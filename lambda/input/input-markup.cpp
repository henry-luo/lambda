#include "input.hpp"
#include "markup-parser.h"
#include "../mark_builder.hpp"
#include "source_tracker.hpp"
#include <string.h>
#include <ctype.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include "lib/log.h"

using namespace lambda;

// Forward declarations for Phase 2 enhanced parsing
static Item parse_document(MarkupParser* parser);
static Item parse_block_element(MarkupParser* parser);
static Item parse_inline_content(MarkupParser* parser, const char* text);

// Phase 3: Forward declarations for enhanced list processing
static Item parse_list_structure(MarkupParser* parser, int base_indent);
static Item parse_nested_list_content(MarkupParser* parser, int base_indent);

// Helper function to increment element content length safely
static void increment_element_content_length(Element* element) {
    if (element && element->type) {
        TypeElmt* elmt_type = (TypeElmt*)element->type;
        elmt_type->content_length++;
    }
}

// Phase 2: Enhanced block element parsers
static Item parse_header(MarkupParser* parser, const char* line);
static Item parse_list_item(MarkupParser* parser, const char* line);
static Item parse_code_block(MarkupParser* parser, const char* line);
static Item parse_indented_code_block(MarkupParser* parser, const char* line);
static Item parse_blockquote(MarkupParser* parser, const char* line);
static Item parse_table_row(MarkupParser* parser, const char* line);
static Item parse_math_block(MarkupParser* parser, const char* line);
static Item parse_paragraph(MarkupParser* parser, const char* line);
static Item parse_divider(MarkupParser* parser);

// Phase 2: Enhanced inline element parsers
static Item parse_inline_spans(MarkupParser* parser, const char* text);
static Item parse_bold_italic(MarkupParser* parser, const char** text);
static Item parse_code_span(MarkupParser* parser, const char** text);
static Item parse_link(MarkupParser* parser, const char** text);
static Item parse_image(MarkupParser* parser, const char** text);
static Item parse_raw_html_tag(MarkupParser* parser, const char** text);

// Phase 4: Advanced inline element parsers
static Item parse_strikethrough(MarkupParser* parser, const char** text);
static Item parse_superscript(MarkupParser* parser, const char** text);
static Item parse_subscript(MarkupParser* parser, const char** text);
static Item parse_emoji_shortcode(MarkupParser* parser, const char** text);
static Item parse_inline_math(MarkupParser* parser, const char** text);
static Item parse_ascii_math_prefix(MarkupParser* parser, const char** text);
static Item parse_small_caps(MarkupParser* parser, const char** text);

// Math parser integration functions
static Item parse_math_content(InputContext& ctx, const char* math_content, const char* flavor);
static const char* detect_math_flavor(const char* content);

// Phase 6: Advanced features - footnotes, citations, directives, metadata
static Item parse_footnote_definition(MarkupParser* parser, const char* line);
static Item parse_footnote_reference(MarkupParser* parser, const char** text);
static Item parse_citation(MarkupParser* parser, const char** text);
static Item parse_rst_directive(MarkupParser* parser, const char* line);
static Item parse_org_block(MarkupParser* parser, const char* line);
static Item parse_yaml_frontmatter(MarkupParser* parser);
static Item parse_org_properties(MarkupParser* parser);
static Item parse_wiki_template(MarkupParser* parser, const char** text);

// MediaWiki-specific forward declarations
static bool is_wiki_heading(const char* line, int* level);
static bool is_wiki_list_item(const char* line, char* marker, int* level);
static bool is_wiki_table_start(const char* line);
static bool is_wiki_table_row(const char* line);
static bool is_wiki_table_end(const char* line);
static bool is_wiki_horizontal_rule(const char* line);
static Item parse_wiki_table(MarkupParser* parser);
static Item parse_wiki_list(MarkupParser* parser);
static Item parse_wiki_link(MarkupParser* parser, const char** text);
static Item parse_wiki_external_link(MarkupParser* parser, const char** text);
static Item parse_wiki_bold_italic(MarkupParser* parser, const char** text);

// RST-specific functions (from old input-rst.cpp)
static bool is_rst_transition_line(const char* line);
static Item parse_rst_transition(MarkupParser* parser);
static bool is_rst_definition_list_item(const char* line);
static bool is_rst_definition_list_definition(const char* line);
static Item parse_rst_definition_list(MarkupParser* parser);
static bool is_rst_literal_block_marker(const char* line);
static bool line_ends_with_double_colon(const char* line);
static Item parse_rst_literal_block(MarkupParser* parser);
static bool is_rst_comment_line(const char* line);
static Item parse_rst_comment(MarkupParser* parser);
static bool is_rst_grid_table_line(const char* line);
static Item parse_rst_grid_table(MarkupParser* parser);
static Item parse_rst_double_backtick_literal(MarkupParser* parser, const char** text);
static Item parse_rst_trailing_underscore_reference(MarkupParser* parser, const char** text);
static bool is_footnote_definition(const char* line);
static bool is_rst_directive(MarkupParser* parser, const char* line);
static bool is_org_block(const char* line);
static bool has_yaml_frontmatter(MarkupParser* parser);
static bool has_org_properties(MarkupParser* parser);

// Textile-specific functions (from old input-textile.cpp)
static bool is_textile_heading(const char* line, int* level);
static bool is_textile_list_item(const char* line, char* list_type);
static bool is_textile_block_code(const char* line);
static bool is_textile_block_quote(const char* line);
static bool is_textile_pre(const char* line);
static bool is_textile_comment(const char* line);
static bool is_textile_notextile(const char* line);
static Item parse_textile_code_block(MarkupParser* parser, const char* line);
static Item parse_textile_block_quote(MarkupParser* parser, const char* line);
static Item parse_textile_pre_block(MarkupParser* parser, const char* line);
static Item parse_textile_comment(MarkupParser* parser, const char* line);
static Item parse_textile_notextile(MarkupParser* parser, const char* line);
static Item parse_textile_list_item(MarkupParser* parser, const char* line);
static Item parse_textile_inline_content(MarkupParser* parser, const char* text);
static char* parse_textile_modifiers(const char* line, int* start_pos);

// AsciiDoc-specific functions (from old input-adoc.cpp)
static bool is_asciidoc_heading(const char* line, int* level);
static bool is_asciidoc_list_item(const char* line);
static bool is_asciidoc_listing_block(const char* line);
static bool is_asciidoc_admonition(const char* line);
static bool is_asciidoc_table_start(const char* line);
static Item parse_asciidoc_heading(MarkupParser* parser, const char* line);
static Item parse_asciidoc_list(MarkupParser* parser);
static Item parse_asciidoc_listing_block(MarkupParser* parser);
static Item parse_asciidoc_admonition(MarkupParser* parser, const char* line);
static Item parse_asciidoc_table(MarkupParser* parser);
static Item parse_asciidoc_inline(MarkupParser* parser, const char* text);
static Item parse_asciidoc_link(MarkupParser* parser, const char** text);

// Phase 5: Forward declarations for enhanced table processing
static Item parse_table_structure(MarkupParser* parser);
static bool is_table_separator(const char* line);
static char* parse_table_alignment(const char* line);
static void apply_table_alignment(Element* table, const char* alignment_spec);
static bool is_table_continuation(const char* line);
static Item parse_table_cell_content(MarkupParser* parser, const char* cell_text);

// Phase 2: Utility functions
static BlockType detect_block_type(MarkupParser* parser, const char* line);
static int get_header_level(MarkupParser* parser, const char* line);
static bool is_list_item(const char* line);
static bool is_code_fence(const char* line);
static bool is_blockquote(const char* line);
static bool is_table_row(const char* line);
static int is_html_block_start(const char* line);  // Returns HTML block type 1-7, or 0 if not
static Item parse_html_block(MarkupParser* parser, const char* line);
static bool is_html_block_end(const char* line, int html_block_type);

// Common utility functions
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define split_lines input_split_lines
#define free_lines input_free_lines

// Local helper functions to replace macros
static inline String* create_string(MarkupParser* parser, const char* str) {
    return parser->builder.createString(str);
}

static inline String* create_symbol(MarkupParser* parser, const char* name) {
    return parser->builder.createSymbol(name);
}

static inline Element* create_element(MarkupParser* parser, const char* tag_name) {
    return parser->builder.element(tag_name).final().element;
}

static inline void add_attribute_to_element(MarkupParser* parser, Element* element, const char* attr_name, const char* attr_value) {
    String* key = parser->builder.createString(attr_name);
    String* value = parser->builder.createString(attr_value);
    if (!key || !value) return;
    Item lambda_value = {.item = s2it(value)};
    parser->builder.putToElement(element, key, lambda_value);
}

// MarkupParser C++ implementation
namespace lambda {

MarkupParser::MarkupParser(Input* input, ParseConfig cfg)
    : InputContext(input)
    , config(cfg)
    , lines(nullptr)
    , line_count(0)
    , current_line(0)
{
    // Initialize state
    resetState();
}

MarkupParser::~MarkupParser() {
    if (lines) {
        free_lines(lines, line_count);
        lines = nullptr;
    }
}

void MarkupParser::resetState() {
    // Reset parsing state
    memset(state.list_markers, 0, sizeof(state.list_markers));
    memset(state.list_levels, 0, sizeof(state.list_levels));
    state.list_depth = 0;

    state.table_state = 0;
    state.in_code_block = false;
    state.code_fence_char = 0;
    state.code_fence_length = 0;

    state.in_math_block = false;
    memset(state.math_delimiter, 0, sizeof(state.math_delimiter));

    // Phase 2: Reset additional state
    state.header_level = 0;
    state.in_quote_block = false;
    state.quote_depth = 0;
    state.in_table = false;
    state.table_columns = 0;
}

Item MarkupParser::parseContent(const char* content) {
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }

    // Split content into lines
    lines = split_lines(content, &line_count);
    if (!lines) {
        return (Item){.item = ITEM_ERROR};
    }

    current_line = 0;
    resetState();

    // Parse the document
    return parse_document(this);
}

} // namespace lambda

// Format detection utilities
MarkupFormat detect_markup_format(const char* content, const char* filename) {
    if (!content) return MARKUP_AUTO_DETECT;

    // File extension-based detection first
    if (filename) {
        const char* ext = strrchr(filename, '.');
        if (ext) {
            ext++; // Skip the dot
            if (strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0) {
                return MARKUP_MARKDOWN;
            } else if (strcasecmp(ext, "rst") == 0) {
                return MARKUP_RST;
            } else if (strcasecmp(ext, "textile") == 0) {
                return MARKUP_TEXTILE;
            } else if (strcasecmp(ext, "wiki") == 0) {
                return MARKUP_WIKI;
            } else if (strcasecmp(ext, "org") == 0) {
                return MARKUP_ORG;
            } else if (strcasecmp(ext, "adoc") == 0 || strcasecmp(ext, "asciidoc") == 0 || strcasecmp(ext, "asc") == 0) {
                return MARKUP_ASCIIDOC;
            } else if (strcasecmp(ext, "man") == 0 ||
                       (ext[0] >= '1' && ext[0] <= '9' && (ext[1] == '\0' || ext[1] == 'm' || ext[1] == 'p'))) {
                // Man pages: .man, .1-.9, .1m, .3p, etc.
                return MARKUP_MAN;
            }
        }
    }

    // Content-based detection
    const char* line = content;
    size_t len = strlen(content);

    // Check for Man page patterns (troff macros)
    // Man pages start with dot-commands like .TH, .SH, .PP, .B, .I
    if ((content[0] == '.' && (strncmp(content, ".TH ", 4) == 0 ||
                                strncmp(content, ".SH ", 4) == 0 ||
                                strncmp(content, ".\\\"", 3) == 0)) ||
        strstr(content, "\n.SH ") || strstr(content, "\n.PP") ||
        strstr(content, "\n.TH ") || strstr(content, "\n.B ") ||
        strstr(content, "\n.I ") || strstr(content, "\n.TP")) {
        return MARKUP_MAN;
    }

    // Check for AsciiDoc patterns
    if (strstr(content, "= ") == content || strstr(content, "== ") || strstr(content, "=== ") ||
        strstr(content, "NOTE:") || strstr(content, "WARNING:") || strstr(content, "TIP:") ||
        strstr(content, "IMPORTANT:") || strstr(content, "CAUTION:") ||
        strstr(content, "----") || strstr(content, "....") ||
        strstr(content, "[source") || strstr(content, "ifdef::") || strstr(content, "ifndef::") ||
        strstr(content, ":toc:") || strstr(content, ":numbered:")) {
        return MARKUP_ASCIIDOC;
    }

    // Check for Org-mode patterns
    if (strstr(content, "#+TITLE:") || strstr(content, "#+AUTHOR:") ||
        strstr(content, "#+BEGIN_SRC") || strstr(content, "* ")) {
        return MARKUP_ORG;
    }

    // Check for reStructuredText patterns
    if (strstr(content, ".. ") || strstr(content, ".. _") ||
        strstr(content, ".. code-block::") || strstr(content, ".. note::") ||
        strstr(content, ".. warning::") || strstr(content, ".. image::") ||
        // Check for RST-style underlined headers (lines with === --- ~~~ ^^^ etc.)
        (strstr(content, "===") && strlen(content) > 10) ||
        (strstr(content, "---") && strlen(content) > 10) ||
        (strstr(content, "~~~") && strlen(content) > 10)) {
        return MARKUP_RST;
    }

    // Check for Textile patterns
    if (strstr(content, "h1.") || strstr(content, "h2.") ||
        strstr(content, "_emphasis_") || strstr(content, "*strong*")) {
        return MARKUP_TEXTILE;
    }

    // Check for Wiki patterns
    if (strstr(content, "== ") || strstr(content, "=== ") ||
        strstr(content, "[[") || strstr(content, "{{")) {
        return MARKUP_WIKI;
    }

    // Default to Markdown for common patterns or unknown
    return MARKUP_MARKDOWN;
}

const char* detect_markup_flavor(MarkupFormat format, const char* content) {
    if (!content) return "standard";

    switch (format) {
        case MARKUP_MARKDOWN:
            if (strstr(content, "```") || strstr(content, "~~") ||
                strstr(content, "- [ ]") || strstr(content, "- [x]")) {
                return "github";
            }
            return "commonmark";

        case MARKUP_WIKI:
            if (strstr(content, "{{") || strstr(content, "[[Category:")) {
                return "mediawiki";
            }
            return "standard";

        case MARKUP_MAN:
            return "groff";

        case MARKUP_RST:
        case MARKUP_TEXTILE:
        case MARKUP_ORG:
        case MARKUP_ASCIIDOC:
        case MARKUP_AUTO_DETECT:
        default:
            return "standard";
    }
}

// Document parsing - creates proper structure according to Doc Schema
static Item parse_document(MarkupParser* parser) {
    // Create root document element according to schema
    Element* doc = create_element(parser, "doc");
    if (!doc) {
        return (Item){.item = ITEM_ERROR};
    }

    // Add version attribute as required by schema
    add_attribute_to_element(parser, doc, "version", "1.0");

    // Create meta element only if there's actual metadata content
    Element* meta = NULL;

    // Phase 6: Parse metadata first (YAML frontmatter, Org properties, or AsciiDoc defaults)
    if (has_yaml_frontmatter(parser)) {
        meta = create_element(parser, "meta");
        if (meta) {
            Item metadata = parse_yaml_frontmatter(parser);
            if (metadata.item != ITEM_UNDEFINED && metadata.item != ITEM_ERROR) {
                list_push((List*)meta, metadata);
                increment_element_content_length(meta);
            }
        }
    } else if (has_org_properties(parser)) {
        meta = create_element(parser, "meta");
        if (meta) {
            Item properties = parse_org_properties(parser);
            if (properties.item != ITEM_UNDEFINED && properties.item != ITEM_ERROR) {
                list_push((List*)meta, properties);
                increment_element_content_length(meta);
            }
        }
    } else if (parser->config.format == MARKUP_ASCIIDOC) {
        // For AsciiDoc: create default metadata like the old parser for compatibility
        meta = create_element(parser, "meta");
        if (meta) {
            // Add default title
            add_attribute_to_element(parser, meta, "title", "AsciiDoc Document");
            // Add default language
            add_attribute_to_element(parser, meta, "language", "en");
        }
    }

    // Add meta element to document only if it was created and has content
    if (meta) {
        list_push((List*)doc, (Item){.item = (uint64_t)meta});
        increment_element_content_length(doc);
    }

    // Create body element for document content
    Element* body = create_element(parser, "body");
    if (!body) {
        return (Item){.item = ITEM_ERROR};
    }

    // Parse content into the body element
    while (parser->current_line < parser->line_count) {
        int line_before = parser->current_line;
        Item block = parse_block_element(parser);
        if (block.item != ITEM_UNDEFINED && block.item != ITEM_ERROR) {
            // Add block elements as children in the List structure
            list_push((List*)body, block);
            // Increment content length for proper element tracking
            increment_element_content_length(body);
        }

        // Safety check: ensure we always advance at least one line to prevent infinite loops
        if (parser->current_line == line_before) {
            parser->current_line++;
        }
    }

    // Add body element to document
    list_push((List*)doc, (Item){.item = (uint64_t)body});
    increment_element_content_length(doc);

    return (Item){.item = (uint64_t)doc};
}

// Phase 2: Enhanced block element parsing
static Item parse_block_element(MarkupParser* parser) {
    if (parser->current_line >= parser->line_count) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    const char* line = parser->lines[parser->current_line];

    // Skip empty lines
    if (is_empty_line(line)) {
        parser->current_line++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Phase 6: Check for advanced features first
    if (is_footnote_definition(line)) {
        return parse_footnote_definition(parser, line);
    }

    if (is_rst_directive(parser, line)) {
        return parse_rst_directive(parser, line);
    }

    if (is_org_block(line)) {
        return parse_org_block(parser, line);
    }

    // Detect block type
    BlockType block_type = detect_block_type(parser, line);

    switch (block_type) {
        case BLOCK_HEADER:
            return parse_header(parser, line);
        case BLOCK_LIST_ITEM:
            // Check if this is an RST definition list
            if (parser->config.format == MARKUP_RST &&
                is_rst_definition_list_item(line) &&
                parser->current_line + 1 < parser->line_count &&
                is_rst_definition_list_definition(parser->lines[parser->current_line + 1])) {
                return parse_rst_definition_list(parser);
            }
            // Check if this is a Textile list item
            if (parser->config.format == MARKUP_TEXTILE) {
                return parse_textile_list_item(parser, line);
            }
            // Check if this is a MediaWiki list item
            if (parser->config.format == MARKUP_WIKI) {
                return parse_wiki_list(parser);
            }
            // Check if this is an AsciiDoc list item
            if (parser->config.format == MARKUP_ASCIIDOC) {
                return parse_asciidoc_list(parser);
            }
            return parse_list_item(parser, line);
        case BLOCK_CODE_BLOCK:
            // Check for RST literal block
            if (parser->config.format == MARKUP_RST &&
                (is_rst_literal_block_marker(line) || line_ends_with_double_colon(line))) {
                return parse_rst_literal_block(parser);
            }
            // Check for Textile block code or pre
            if (parser->config.format == MARKUP_TEXTILE) {
                if (is_textile_block_code(line)) {
                    return parse_textile_code_block(parser, line);
                } else if (is_textile_pre(line)) {
                    return parse_textile_pre_block(parser, line);
                }
            }
            // Check for AsciiDoc listing block
            if (parser->config.format == MARKUP_ASCIIDOC && is_asciidoc_listing_block(line)) {
                return parse_asciidoc_listing_block(parser);
            }
            // Check for indented code block (4+ leading spaces, no fence)
            {
                int space_count = 0;
                const char* p = line;
                while (*p == ' ') { space_count++; p++; }
                if (space_count >= 4 && *p != '`' && *p != '~') {
                    return parse_indented_code_block(parser, line);
                }
            }
            return parse_code_block(parser, line);
        case BLOCK_QUOTE:
            // Check for Textile block quote
            if (parser->config.format == MARKUP_TEXTILE && is_textile_block_quote(line)) {
                return parse_textile_block_quote(parser, line);
            }
            // Check for AsciiDoc admonition
            if (parser->config.format == MARKUP_ASCIIDOC && is_asciidoc_admonition(line)) {
                return parse_asciidoc_admonition(parser, line);
            }
            return parse_blockquote(parser, line);
        case BLOCK_TABLE:
            // Check for RST grid table
            if (parser->config.format == MARKUP_RST && is_rst_grid_table_line(line)) {
                return parse_rst_grid_table(parser);
            }
            // Check for MediaWiki table
            if (parser->config.format == MARKUP_WIKI && is_wiki_table_start(line)) {
                return parse_wiki_table(parser);
            }
            // Check for AsciiDoc table
            if (parser->config.format == MARKUP_ASCIIDOC && is_asciidoc_table_start(line)) {
                return parse_asciidoc_table(parser);
            }
            return parse_table_structure(parser);
        case BLOCK_MATH:
            return parse_math_block(parser, line);
        case BLOCK_DIVIDER:
            // Check for RST transition line
            if (parser->config.format == MARKUP_RST && is_rst_transition_line(line)) {
                return parse_rst_transition(parser);
            }
            // Check for MediaWiki horizontal rule
            if (parser->config.format == MARKUP_WIKI && is_wiki_horizontal_rule(line)) {
                parser->current_line++;
                return parse_divider(parser);
            }
            parser->current_line++;
            return parse_divider(parser);
        case BLOCK_COMMENT:
            // Check for Textile comment or notextile
            if (parser->config.format == MARKUP_TEXTILE) {
                if (is_textile_comment(line)) {
                    return parse_textile_comment(parser, line);
                } else if (is_textile_notextile(line)) {
                    return parse_textile_notextile(parser, line);
                }
            }
            return parse_rst_comment(parser);
        case BLOCK_HTML:
            return parse_html_block(parser, line);
        case BLOCK_PARAGRAPH:
        default:
            return parse_paragraph(parser, line);
    }
}

// Parse header elements (# Header, ## Header, etc.) - Creates HTML-like h1, h2, etc.
static Item parse_header(MarkupParser* parser, const char* line) {
    int level = get_header_level(parser, line);
    if (level == 0) {
        // Fallback to paragraph if not a valid header
        return parse_paragraph(parser, line);
    }

    // Create appropriate header element (h1, h2, h3, h4, h5, h6)
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", level);
    Element* header = create_element(parser, tag_name);
    if (!header) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Add level attribute for compatibility
    char level_str[8];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(parser, header, "level", level_str);

    // Extract header text (handle different markup styles)
    const char* text = line;
    char* header_text_buf = NULL;  // buffer for cleaned header text
    char* wiki_text_buf = NULL;    // buffer for wiki heading text

    if (*text == '#' || (*text == ' ' && strchr(text, '#'))) {
        // Markdown-style header: skip leading spaces (up to 3) and # markers
        const char* pos = text;
        int leading_spaces = 0;
        while (*pos == ' ' && leading_spaces < 3) {
            pos++;
            leading_spaces++;
        }
        // Skip # markers
        while (*pos == '#') pos++;
        // Skip all whitespace after # markers (CommonMark: content starts after first space)
        while (*pos == ' ' || *pos == '\t') {
            pos++;
        }

        // Now pos points to start of content
        // Find end of content (strip trailing # markers and spaces)
        size_t content_len = strlen(pos);
        if (content_len > 0) {
            // Allocate buffer for cleaned content
            header_text_buf = (char*)malloc(content_len + 1);
            strcpy(header_text_buf, pos);

            // Strip trailing spaces first
            int end = content_len - 1;
            while (end >= 0 && (header_text_buf[end] == ' ' || header_text_buf[end] == '\t')) {
                end--;
            }

            // Strip trailing # markers only if preceded by a space OR if all content is #
            // First check if trailing characters are #
            int hash_start = end;
            while (hash_start >= 0 && header_text_buf[hash_start] == '#') {
                hash_start--;
            }

            // Strip trailing # if:
            // 1. There's a space before them (hash_start >= 0 && is space), OR
            // 2. ALL content is hashes (hash_start < 0)
            if ((hash_start >= 0 && hash_start < end && header_text_buf[hash_start] == ' ') ||
                (hash_start < 0 && end >= 0)) {
                if (hash_start >= 0) {
                    end = hash_start;
                    // Strip any spaces before the closing #
                    while (end >= 0 && (header_text_buf[end] == ' ' || header_text_buf[end] == '\t')) {
                        end--;
                    }
                } else {
                    // All content was hashes - empty header
                    end = -1;
                }
            }

            header_text_buf[end + 1] = '\0';
            text = header_text_buf;
        } else {
            // Empty header
            text = "";
        }
    } else if (parser->config.format == MARKUP_WIKI && *text == '=') {
        // Wiki-style header: == Header Text ==
        // Skip leading = markers and whitespace
        while (*text == '=') text++;
        skip_whitespace(&text);

        // Find and strip trailing = markers and whitespace
        int text_len = strlen(text);
        if (text_len > 0) {
            // Find the end of actual content (before trailing = markers)
            int end_pos = text_len - 1;
            while (end_pos >= 0 && text[end_pos] == '=') end_pos--;
            // Trim trailing whitespace before the = markers
            while (end_pos >= 0 && (text[end_pos] == ' ' || text[end_pos] == '\t')) end_pos--;

            // Create a trimmed copy
            if (end_pos >= 0) {
                wiki_text_buf = (char*)malloc(end_pos + 2);
                strncpy(wiki_text_buf, text, end_pos + 1);
                wiki_text_buf[end_pos + 1] = '\0';
                text = wiki_text_buf;
            }
        }
    } else if (parser->config.format == MARKUP_RST) {
        // RST-style underlined header: use the entire line as text
        // The underline will be skipped by incrementing current_line twice
        text = line;
        skip_whitespace(&text);
    } else if ((parser->config.format == MARKUP_MARKDOWN) &&
               *text != '#') {
        // CommonMark setext heading: text line followed by === or ---
        // Trim leading/trailing whitespace from content line
        const char* start = text;
        // Skip up to 3 leading spaces
        int leading = 0;
        while (*start == ' ' && leading < 3) {
            start++;
            leading++;
        }
        // Skip trailing whitespace and tabs
        size_t content_len = strlen(start);
        if (content_len > 0) {
            header_text_buf = (char*)malloc(content_len + 1);
            strcpy(header_text_buf, start);
            int end = content_len - 1;
            while (end >= 0 && (header_text_buf[end] == ' ' || header_text_buf[end] == '\t')) {
                end--;
            }
            header_text_buf[end + 1] = '\0';
            text = header_text_buf;
        } else {
            text = "";
        }
    } else {
        // Fallback: use the entire line
        skip_whitespace(&text);
    }

    // Parse inline content for header text and add as children
    Item content = parse_inline_spans(parser, text);
    if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
        list_push((List*)header, content);
        increment_element_content_length(header);
    }

    // Free temporary buffers
    if (header_text_buf) {
        free(header_text_buf);
    }
    if (wiki_text_buf) {
        free(wiki_text_buf);
    }

    parser->current_line++;

    // For RST underlined headers, also skip the underline
    if (parser->config.format == MARKUP_RST &&
        parser->current_line < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line];
        const char* next_pos = next_line;
        skip_whitespace(&next_pos);

        // Check if next line is an underline
        char ch = *next_pos;
        if (ch && (ch == '=' || ch == '-' || ch == '~' ||
                  ch == '^' || ch == '+' || ch == '*')) {
            // Skip the underline
            parser->current_line++;
        }
    }

    // For CommonMark setext headings, also skip the underline
    if ((parser->config.format == MARKUP_MARKDOWN) &&
        parser->current_line < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line];
        const char* next_pos = next_line;
        // Skip up to 3 spaces
        int spaces = 0;
        while (*next_pos == ' ' && spaces < 3) {
            next_pos++;
            spaces++;
        }

        // Check if next line is a setext underline (= or -)
        char ch = *next_pos;
        if (ch == '=' || ch == '-') {
            // Verify it's all the same character
            char underline_char = ch;
            int count = 0;
            while (*next_pos == underline_char) {
                next_pos++;
                count++;
            }
            // Skip trailing whitespace
            while (*next_pos == ' ' || *next_pos == '\t') {
                next_pos++;
            }
            // Valid underline ends with newline or end of string
            if (count >= 1 && (*next_pos == '\0' || *next_pos == '\n' || *next_pos == '\r')) {
                // Skip the underline
                parser->current_line++;
            }
        }
    }

    return (Item){.item = (uint64_t)header};
}

// Parse paragraph with enhanced inline parsing - Creates HTML-like <p> element
static Item parse_paragraph(MarkupParser* parser, const char* line) {
    Element* para = create_element(parser, "p");
    if (!para) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Use StrBuf to build content from potentially multiple lines
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    // For the first line, always add it to the paragraph
    const char* first_line = parser->lines[parser->current_line];
    skip_whitespace(&first_line);
    stringbuf_append_str(sb, first_line);
    parser->current_line++;

    // Check if we should continue collecting lines for this paragraph
    // Don't join lines that contain math expressions to avoid malformed expressions
    bool first_line_has_math = (strstr(first_line, "$") != NULL);

    if (!first_line_has_math) {
        // Only collect additional lines if the first line doesn't have math
        while (parser->current_line < parser->line_count) {
            const char* current = parser->lines[parser->current_line];

            if (is_empty_line(current)) {
                break; // End of paragraph
            }

            BlockType next_type = detect_block_type(parser, current);
            if (next_type != BLOCK_PARAGRAPH) {
                break; // Next line is different block type
            }

            const char* content = current;
            skip_whitespace(&content);

            // Don't join lines that contain math expressions
            if (strstr(content, "$") != NULL) {
                break;
            }

            // CommonMark soft line break: use newline, not space
            stringbuf_append_char(sb, '\n');
            stringbuf_append_str(sb, content);
            parser->current_line++;
        }
    }

    // Parse inline content with enhancements and add as children
    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
    Item content = parse_inline_spans(parser, text_content->chars);

    if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
        list_push((List*)para, content);
        increment_element_content_length(para);
    }

    return (Item){.item = (uint64_t)para};
}

// Phase 3: Enhanced list processing with multi-level nesting
static int get_list_indentation(const char* line) {
    if (!line) return 0;
    int indent = 0;
    while (*line == ' ' || *line == '\t') {
        if (*line == ' ') indent++;
        else if (*line == '\t') indent += 4; // Tab counts as 4 spaces
        line++;
    }
    return indent;
}

static char get_list_marker(const char* line) {
    if (!line) return 0;
    const char* pos = line;
    skip_whitespace(&pos);

    // Check for unordered markers
    if (*pos == '-' || *pos == '*' || *pos == '+') {
        return *pos;
    }

    // Check for ordered markers (return '.' for any numbered list)
    if (isdigit(*pos)) {
        while (isdigit(*pos)) pos++;
        if (*pos == '.' || *pos == ')') return '.';
    }

    return 0;
}

static bool is_ordered_marker(char marker) {
    return marker == '.';
}

static Item parse_nested_list_content(MarkupParser* parser, int base_indent) {
    Element* content_container = create_element(parser, "div");
    if (!content_container) return (Item){.item = ITEM_ERROR};

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        if (is_empty_line(line)) {
            parser->current_line++;
            continue;
        }

        int line_indent = get_list_indentation(line);

        // If line is at or before base indentation and is a list item, we're done
        if (line_indent <= base_indent && is_list_item(line)) {
            break;
        }

        // If line is less indented than expected, we're done with this content
        if (line_indent < base_indent + 2) {
            break;
        }

        // Check if this starts a nested list
        if (is_list_item(line)) {
            Item nested_list = parse_list_structure(parser, line_indent);
            if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                list_push((List*)content_container, nested_list);
                increment_element_content_length(content_container);
            }
        } else {
            // Check what type of block this is
            BlockType block_type = detect_block_type(parser, line);
            if (block_type == BLOCK_CODE_BLOCK) {
                // Parse as code block directly
                Item code_content = parse_code_block(parser, line);
                if (code_content.item != ITEM_ERROR && code_content.item != ITEM_UNDEFINED) {
                    list_push((List*)content_container, code_content);
                    increment_element_content_length(content_container);
                }
            } else {
                // Parse as paragraph content
                Item para_content = parse_paragraph(parser, line);
                if (para_content.item != ITEM_ERROR && para_content.item != ITEM_UNDEFINED) {
                    list_push((List*)content_container, para_content);
                    increment_element_content_length(content_container);
                } else {
                    // If paragraph parsing failed and didn't advance, advance manually to avoid infinite loop
                    parser->current_line++;
                }
            }
        }
    }

    return (Item){.item = (uint64_t)content_container};
}

// Phase 3: Enhanced list structure parsing with proper nesting
static Item parse_list_structure(MarkupParser* parser, int base_indent) {
    if (parser->current_line >= parser->line_count) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    const char* first_line = parser->lines[parser->current_line];
    char marker = get_list_marker(first_line);
    bool is_ordered = is_ordered_marker(marker);

    // Create the appropriate list container
    Element* list = create_element(parser, is_ordered ? "ol" : "ul");
    if (!list) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Track list state for proper nesting
    if (parser->state.list_depth < 9) {
        parser->state.list_markers[parser->state.list_depth] = marker;
        parser->state.list_levels[parser->state.list_depth] = base_indent;
        parser->state.list_depth++;
    }

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        if (is_empty_line(line)) {
            // Skip empty lines but check if list continues
            int next_line = parser->current_line + 1;
            if (next_line >= parser->line_count) break;

            const char* next = parser->lines[next_line];
            int next_indent = get_list_indentation(next);

            // If next line continues the list or is content for current item
            if ((is_list_item(next) && next_indent >= base_indent) ||
                (!is_list_item(next) && next_indent > base_indent)) {
                parser->current_line++;
                continue;
            } else {
                break; // End of list
            }
        }

        int line_indent = get_list_indentation(line);

        // If this line is less indented than our base, we're done with this list
        if (line_indent < base_indent) {
            break;
        }

        // If this is a list item at our level
        if (line_indent == base_indent && is_list_item(line)) {
            char line_marker = get_list_marker(line);
            bool line_is_ordered = is_ordered_marker(line_marker);

            // Check if this item belongs to our list type
            if (line_is_ordered != is_ordered) {
                break; // Different list type, end current list
            }

            // Create list item
            Element* item = create_element(parser, "li");
            if (!item) break;

            // Extract content after marker
            const char* item_content = line;
            skip_whitespace(&item_content);

            // Skip list marker
            if (line_is_ordered) {
                while (isdigit(*item_content)) item_content++;
                if (*item_content == '.' || *item_content == ')') item_content++;
            } else {
                item_content++; // Skip single character marker
            }
            skip_whitespace(&item_content);

            // Parse immediate inline content
            if (*item_content) {
                Item text_content = parse_inline_spans(parser, item_content);
                if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                    list_push((List*)item, text_content);
                    increment_element_content_length(item);
                }
            }

            parser->current_line++;

            // Look for continued content (nested lists, paragraphs)
            Item nested_content = parse_nested_list_content(parser, base_indent);
            if (nested_content.item != ITEM_ERROR && nested_content.item != ITEM_UNDEFINED) {
                Element* content_div = (Element*)nested_content.item;
                if (content_div && ((List*)content_div)->length > 0) {
                    // Move contents from div to list item
                    List* div_list = (List*)content_div;
                    for (long i = 0; i < div_list->length; i++) {
                        list_push((List*)item, div_list->items[i]);
                        increment_element_content_length(item);
                    }
                }
            }

            // Add completed list item to list
            list_push((List*)list, (Item){.item = (uint64_t)item});
            increment_element_content_length(list);

        } else if (line_indent > base_indent && is_list_item(line)) {
            // This is a nested list - parse it recursively
            Item nested_list = parse_list_structure(parser, line_indent);
            if (nested_list.item != ITEM_ERROR && nested_list.item != ITEM_UNDEFINED) {
                // Add nested list to the last list item if it exists
                List* current_list = (List*)list;
                if (current_list->length > 0) {
                    Element* last_item = (Element*)current_list->items[current_list->length - 1].item;
                    list_push((List*)last_item, nested_list);
                    increment_element_content_length(last_item);
                }
            }
        } else {
            // Not a list item and not properly indented, end list
            break;
        }
    }

    // Pop list state
    if (parser->state.list_depth > 0) {
        parser->state.list_depth--;
        parser->state.list_markers[parser->state.list_depth] = 0;
        parser->state.list_levels[parser->state.list_depth] = 0;
    }

    return (Item){.item = (uint64_t)list};
}

// Parse list items (-, *, +, 1., 2., etc.) - Enhanced with nesting support
static Item parse_list_item(MarkupParser* parser, const char* line) {
    int base_indent = get_list_indentation(line);
    return parse_list_structure(parser, base_indent);
}

// Parse code blocks (```, ```, ~~~, etc.)
static Item parse_code_block(MarkupParser* parser, const char* line) {
    // Create code element with type="block" attribute
    Element* code = create_element(parser, "code");
    if (!code) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Mark as block-level code
    add_attribute_to_element(parser, code, "type", "block");

    // Extract fence character, length, and indentation from opening fence
    const char* start = line;
    int fence_indent = 0;
    while (*start == ' ' && fence_indent < 3) {
        start++;
        fence_indent++;
    }

    char fence_char = *start;
    int fence_len = 0;
    const char* fence = start;
    while (*fence == fence_char) {
        fence_len++;
        fence++;
    }

    // Extract info string (language) after fence
    char lang[64] = {0};
    const char* info_start = fence;
    // Skip leading spaces
    while (*info_start == ' ' || *info_start == '\t') info_start++;

    if (*info_start && *info_start != '\n' && *info_start != '\r') {
        // For backtick fences, info string cannot contain backticks
        if (fence_char == '`' && strchr(info_start, '`')) {
            // Invalid - treat as paragraph
            return parse_paragraph(parser, line);
        }

        // Extract first word as language (up to first space)
        int i = 0;
        while (info_start[i] && info_start[i] != ' ' && info_start[i] != '\t' &&
               info_start[i] != '\n' && info_start[i] != '\r' && i < 63) {
            lang[i] = info_start[i];
            i++;
        }
        lang[i] = '\0';

        if (lang[0]) {
            // Check if this is an ASCII math block
            if (strcmp(lang, "asciimath") == 0 || strcmp(lang, "ascii-math") == 0) {
                // Convert code block to math block
                Element* math = create_element(parser, "math");
                if (!math) {
                    parser->current_line++;
                    return (Item){.item = ITEM_ERROR};
                }

                add_attribute_to_element(parser, math, "type", "block");
                add_attribute_to_element(parser, math, "flavor", "ascii");

                parser->current_line++; // Skip opening fence

                // Collect math content until closing fence
                StringBuf* sb = parser->sb;
                stringbuf_reset(sb);

                while (parser->current_line < parser->line_count) {
                    const char* current = parser->lines[parser->current_line];

                    // Check for closing fence (same char, same or longer length)
                    const char* check = current;
                    int check_indent = 0;
                    while (*check == ' ' && check_indent < 3) { check++; check_indent++; }
                    if (*check == fence_char) {
                        int close_len = 0;
                        while (*check == fence_char) { close_len++; check++; }
                        // Must be followed only by spaces/tabs/end
                        while (*check == ' ' || *check == '\t') check++;
                        if ((*check == '\0' || *check == '\n' || *check == '\r') && close_len >= fence_len) {
                            parser->current_line++; // Skip closing fence
                            break;
                        }
                    }

                    // Add line to math content
                    if (sb->length > 0) {
                        stringbuf_append_char(sb, '\n');
                    }
                    stringbuf_append_str(sb, current);
                    parser->current_line++;
                }

                // Parse the math content using ASCII flavor
                String* math_content_str = parser->builder.createString(sb->str->chars, sb->length);
                InputContext math_ctx(parser->input(), math_content_str->chars, math_content_str->len);
                Item parsed_math = parse_math_content(math_ctx, math_content_str->chars, "ascii");

                if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
                    list_push((List*)math, parsed_math);
                    increment_element_content_length(math);
                } else {
                    // Fallback to plain text if math parsing fails
                    Item math_item = {.item = s2it(math_content_str)};
                    list_push((List*)math, math_item);
                    increment_element_content_length(math);
                }

                return (Item){.item = (uint64_t)math};
            }

            add_attribute_to_element(parser, code, "language", lang);
            add_attribute_to_element(parser, code, "info", lang);  // Also set info attribute
        }
    }

    parser->current_line++; // Skip opening fence

    // Collect code content until closing fence
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check for closing fence (same char, same or longer length)
        const char* check = current;
        int check_indent = 0;
        while (*check == ' ' && check_indent < 3) { check++; check_indent++; }
        if (*check == fence_char) {
            int close_len = 0;
            while (*check == fence_char) { close_len++; check++; }
            // Must be followed only by spaces/tabs/end
            while (*check == ' ' || *check == '\t') check++;
            if ((*check == '\0' || *check == '\n' || *check == '\r') && close_len >= fence_len) {
                parser->current_line++; // Skip closing fence
                break;
            }
        }

        // Remove up to fence_indent spaces from content line
        const char* content = current;
        int spaces_removed = 0;
        while (*content == ' ' && spaces_removed < fence_indent) {
            content++;
            spaces_removed++;
        }

        // Add line to code content (with newline after each line)
        stringbuf_append_str(sb, content);
        stringbuf_append_char(sb, '\n');
        parser->current_line++;
    }

    // Create code content (no inline parsing for code blocks)
    String* code_content = parser->builder.createString(sb->str->chars, sb->length);
    Item text_item = {.item = s2it(code_content)};
    list_push((List*)code, text_item);
    increment_element_content_length(code);

    return (Item){.item = (uint64_t)code};
}

// Parse indented code blocks (CommonMark: 4+ leading spaces)
static Item parse_indented_code_block(MarkupParser* parser, const char* line) {
    // Create code element with type="block" attribute
    Element* code = create_element(parser, "code");
    if (!code) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Mark as block-level code
    add_attribute_to_element(parser, code, "type", "block");

    // Collect code content
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Count leading spaces
        int space_count = 0;
        const char* p = current;
        while (*p == ' ') { space_count++; p++; }

        // Check for end of indented code block
        // - 4+ spaces: continue code block (strip 4 spaces)
        // - blank line: include in code block (as empty line)
        // - otherwise: end code block
        bool is_blank = (*p == '\0' || *p == '\n' || *p == '\r');
        if (space_count >= 4) {
            // Strip exactly 4 spaces and add content
            stringbuf_append_str(sb, current + 4);
            stringbuf_append_char(sb, '\n');
            parser->current_line++;
        } else if (is_blank) {
            // Blank line inside indented code block
            stringbuf_append_char(sb, '\n');
            parser->current_line++;
        } else {
            // End of indented code block
            break;
        }
    }

    // Create code content
    String* code_content = parser->builder.createString(sb->str->chars, sb->length);
    Item text_item = {.item = s2it(code_content)};
    list_push((List*)code, text_item);
    increment_element_content_length(code);

    return (Item){.item = (uint64_t)code};
}

// Parse horizontal divider/rule
static Item parse_divider(MarkupParser* parser) {
    Element* hr = create_element(parser, "hr");
    if (!hr) {
        return (Item){.item = ITEM_ERROR};
    }

    return (Item){.item = (uint64_t)hr};
}

// Parse blockquote elements (> quoted text)
// CommonMark: Blockquotes can contain multiple lines and nested blocks
static Item parse_blockquote(MarkupParser* parser, const char* line) {
    Element* quote = create_element(parser, "blockquote");
    if (!quote) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Collect all lines that belong to this blockquote
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        const char* pos = current;

        // Skip up to 3 leading spaces
        int spaces = 0;
        while (*pos == ' ' && spaces < 3) { spaces++; pos++; }

        // Check if line starts with >
        if (*pos == '>') {
            pos++; // Skip >
            // Skip optional space after >
            if (*pos == ' ') pos++;

            // Add content after > to buffer
            if (sb->length > 0) {
                stringbuf_append_char(sb, '\n');
            }
            stringbuf_append_str(sb, pos);
            parser->current_line++;
        } else if (*pos == '\0' || *pos == '\n' || *pos == '\r') {
            // Blank line ends blockquote (unless followed by >)
            if (parser->current_line + 1 < parser->line_count) {
                const char* next = parser->lines[parser->current_line + 1];
                const char* next_pos = next;
                int ns = 0;
                while (*next_pos == ' ' && ns < 3) { ns++; next_pos++; }
                if (*next_pos == '>') {
                    // Blank line before continuation - include as empty line
                    if (sb->length > 0) stringbuf_append_char(sb, '\n');
                    parser->current_line++;
                    continue;
                }
            }
            // End of blockquote
            break;
        } else {
            // Lazy continuation: paragraph lines can continue blockquote
            // Check if we're in a paragraph context (previous content was text)
            // For now, end the blockquote on non-> lines
            break;
        }
    }

    // Now parse the collected content as blocks
    // Create a temporary parser context for the blockquote content
    if (sb->length > 0) {
        String* quote_content = parser->builder.createString(sb->str->chars, sb->length);

        // Split into lines and parse as blocks
        char* content_copy = strdup(quote_content->chars);
        if (content_copy) {
            // Tokenize by newlines
            char* saveptr = NULL;
            char* line_token = strtok_r(content_copy, "\n", &saveptr);
            StringBuf* para_buf = stringbuf_new(parser->input()->pool);

            while (line_token != NULL) {
                const char* lpos = line_token;
                skip_whitespace(&lpos);

                // Detect block type of this line
                if (*lpos == '#') {
                    // Flush any paragraph content
                    if (para_buf->length > 0) {
                        Element* p = create_element(parser, "p");
                        if (p) {
                            Item inline_content = parse_inline_spans(parser, para_buf->str->chars);
                            if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                                list_push((List*)p, inline_content);
                                increment_element_content_length(p);
                            }
                            list_push((List*)quote, (Item){.item = (uint64_t)p});
                            increment_element_content_length(quote);
                        }
                        stringbuf_reset(para_buf);
                    }

                    // Parse header
                    int level = 0;
                    while (*lpos == '#' && level < 6) { level++; lpos++; }
                    while (*lpos == ' ' || *lpos == '\t') lpos++;

                    // Strip trailing # and spaces
                    char* header_text = strdup(lpos);
                    if (header_text) {
                        int end = strlen(header_text) - 1;
                        while (end >= 0 && (header_text[end] == ' ' || header_text[end] == '\t')) end--;
                        while (end >= 0 && header_text[end] == '#') end--;
                        while (end >= 0 && (header_text[end] == ' ' || header_text[end] == '\t')) end--;
                        header_text[end + 1] = '\0';

                        char tag[4];
                        snprintf(tag, sizeof(tag), "h%d", level);
                        Element* h = create_element(parser, tag);
                        if (h) {
                            Item h_content = parse_inline_spans(parser, header_text);
                            if (h_content.item != ITEM_ERROR && h_content.item != ITEM_UNDEFINED) {
                                list_push((List*)h, h_content);
                                increment_element_content_length(h);
                            }
                            list_push((List*)quote, (Item){.item = (uint64_t)h});
                            increment_element_content_length(quote);
                        }
                        free(header_text);
                    }
                } else if (*lpos == '\0') {
                    // Empty line - flush paragraph
                    if (para_buf->length > 0) {
                        Element* p = create_element(parser, "p");
                        if (p) {
                            Item inline_content = parse_inline_spans(parser, para_buf->str->chars);
                            if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                                list_push((List*)p, inline_content);
                                increment_element_content_length(p);
                            }
                            list_push((List*)quote, (Item){.item = (uint64_t)p});
                            increment_element_content_length(quote);
                        }
                        stringbuf_reset(para_buf);
                    }
                } else {
                    // Paragraph text
                    if (para_buf->length > 0) {
                        stringbuf_append_char(para_buf, '\n');
                    }
                    stringbuf_append_str(para_buf, lpos);
                }

                line_token = strtok_r(NULL, "\n", &saveptr);
            }

            // Flush final paragraph
            if (para_buf->length > 0) {
                Element* p = create_element(parser, "p");
                if (p) {
                    Item inline_content = parse_inline_spans(parser, para_buf->str->chars);
                    if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                        list_push((List*)p, inline_content);
                        increment_element_content_length(p);
                    }
                    list_push((List*)quote, (Item){.item = (uint64_t)p});
                    increment_element_content_length(quote);
                }
            }

            free(content_copy);
        }
    }

    return (Item){.item = (uint64_t)quote};
}

// Parse math blocks ($$...$$)
static Item parse_math_block(MarkupParser* parser, const char* line) {
    Element* math = create_element(parser, "math");
    if (!math) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, math, "type", "block");

    // Check if this is single-line block math ($$content$$)
    const char* pos = line;
    skip_whitespace(&pos);
    if (*pos == '$' && *(pos+1) == '$') {
        pos += 2; // Skip opening $$

        // Find closing $$
        const char* end = strstr(pos, "$$");
        if (end && end > pos) {
            // Single-line block math
            size_t content_len = end - pos;
            char* math_content = (char*)malloc(content_len + 1);
            if (math_content) {
                strncpy(math_content, pos, content_len);
                math_content[content_len] = '\0';

                const char* math_flavor = detect_math_flavor(math_content);
                InputContext math_ctx(parser->input(), math_content, content_len);
                Item parsed_math = parse_math_content(math_ctx, math_content, math_flavor);

                if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
                    list_push((List*)math, parsed_math);
                    increment_element_content_length(math);
                } else {
                    // Fallback to plain text if math parsing fails
                    String* content_str = create_string(parser, math_content);
                    Item text_item = {.item = s2it(content_str)};
                    list_push((List*)math, text_item);
                    increment_element_content_length(math);
                }

                free(math_content);
            }
            parser->current_line++; // Move to next line
            return (Item){.item = (uint64_t)math};
        }
    }

    // Multi-line block math - original logic
    parser->current_line++; // Skip opening $$

    // Collect math content until closing $$
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Check for closing $$ at start of line
        const char* pos = current;
        skip_whitespace(&pos);
        if (*pos == '$' && *(pos+1) == '$') {
            parser->current_line++; // Skip closing $$
            break;
        }

        // Check for closing $$ at end of line (for inline-style multi-line blocks)
        size_t line_len = strlen(current);
        if (line_len >= 2) {
            const char* line_end = current + line_len;
            // Skip trailing whitespace
            while (line_end > current && (*(line_end-1) == ' ' || *(line_end-1) == '\t')) {
                line_end--;
            }
            if (line_end - current >= 2 && *(line_end-2) == '$' && *(line_end-1) == '$') {
                // Found $$ at end of line - add content before $$
                size_t content_len = (line_end - 2) - current;
                if (sb->length > 0) {
                    stringbuf_append_char(sb, '\n');
                }
                if (content_len > 0) {
                    stringbuf_append_str_n(sb, current, content_len);
                }
                parser->current_line++; // Skip this line with closing $$
                break;
            }
        }

        // Add line to math content
        if (sb->length > 0) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, current);
        parser->current_line++;
    }

    // Parse the math content using the math parser
    String* math_content_str = parser->builder.createString(sb->str->chars, sb->length);
    const char* math_flavor = detect_math_flavor(math_content_str->chars);

    InputContext math_ctx(parser->input(), math_content_str->chars, math_content_str->len);
    Item parsed_math = parse_math_content(math_ctx, math_content_str->chars, math_flavor);
    if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
        list_push((List*)math, parsed_math);
        increment_element_content_length(math);
    } else {
        // Fallback to plain text if math parsing fails
        Item text_item = {.item = s2it(math_content_str)};
        list_push((List*)math, text_item);
        increment_element_content_length(math);
    }

    return (Item){.item = (uint64_t)math};
}

// Phase 5: Enhanced table parsing with alignment and multi-line support
// Phase 5: Enhanced table parsing with alignment and multi-line support
static Item parse_table_structure(MarkupParser* parser) {
    if (parser->current_line >= parser->line_count) {
        return (Item){.item = ITEM_ERROR};
    }

    // Create table element
    Element* table = create_element(parser, "table");
    if (!table) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    const char* first_line = parser->lines[parser->current_line];

    // Check if next line is a separator (for header detection)
    bool has_header = false;
    char* alignment_spec = NULL;

    if (parser->current_line + 1 < parser->line_count) {
        const char* next_line = parser->lines[parser->current_line + 1];
        if (is_table_separator(next_line)) {
            has_header = true;
            alignment_spec = parse_table_alignment(next_line);
        }
    }

    // Apply alignment to table if available
    if (alignment_spec) {
        add_attribute_to_element(parser, table, "align", alignment_spec);
        free(alignment_spec);
        alignment_spec = NULL;
    }

    // Parse header row if present
    if (has_header) {
        Element* thead = create_element(parser, "thead");
        if (thead) {
            // Parse the header row and convert cells to th elements
            const char* header_line = parser->lines[parser->current_line];
            Element* header_row = create_element(parser, "tr");
            if (header_row) {
                // Parse header row cells manually and create th elements
                const char* pos = header_line;
                skip_whitespace(&pos);

                // Skip leading | if present
                if (*pos == '|') pos++;

                while (*pos) {
                    // Find next | or end of line
                    const char* cell_start = pos;
                    const char* cell_end = pos;

                    while (*cell_end && *cell_end != '|') {
                        cell_end++;
                    }

                    // Extract cell content
                    size_t cell_len = cell_end - cell_start;
                    char* cell_text = (char*)malloc(cell_len + 1);
                    if (!cell_text) break;

                    strncpy(cell_text, cell_start, cell_len);
                    cell_text[cell_len] = '\0';

                    // Create table header cell
                    Element* th_cell = create_element(parser, "th");
                    if (th_cell) {
                        // Parse cell content with enhanced formatting
                        Item cell_content = parse_table_cell_content(parser, cell_text);
                        if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                            list_push((List*)th_cell, cell_content);
                            increment_element_content_length(th_cell);
                        }

                        // Add cell to row
                        list_push((List*)header_row, (Item){.item = (uint64_t)th_cell});
                        increment_element_content_length(header_row);
                    }

                    free(cell_text);

                    // Move to next cell
                    pos = cell_end;
                    if (*pos == '|') pos++;

                    if (!*pos) break;
                }

                // Add header row to thead
                list_push((List*)thead, (Item){.item = (uint64_t)header_row});
                increment_element_content_length(thead);
            }

            // Add thead to table
            list_push((List*)table, (Item){.item = (uint64_t)thead});
            increment_element_content_length(table);
        }

        // Skip header line and separator line
        parser->current_line += 2;
    }

    // Create tbody for data rows
    Element* tbody = create_element(parser, "tbody");
    if (!tbody) {
        parser->current_line++;
        return (Item){.item = (uint64_t)table};
    }

    // Parse data rows
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        if (!is_table_continuation(line)) {
            break;
        }

        Item row = parse_table_row(parser, line);
        if (row.item != ITEM_ERROR && row.item != ITEM_UNDEFINED) {
            list_push((List*)tbody, row);
            increment_element_content_length(tbody);
        }
    }

    // Add tbody to table if it has content
    if (tbody && ((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, (Item){.item = (uint64_t)tbody});
        increment_element_content_length(table);
    }

    return (Item){.item = (uint64_t)table};
}

// Check if line is a table separator (e.g., |---|---|)
static bool is_table_separator(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Must start with |
    if (*pos != '|') return false;
    pos++;

    // Check pattern: spaces, dashes, colons, pipes
    bool found_dash = false;
    bool found_any_dash = false;  // track if we found at least one dash in any column
    while (*pos) {
        if (*pos == '|') {
            if (!found_dash) return false; // Must have at least one dash per column
            found_any_dash = true;
            found_dash = false;
            pos++;
        } else if (*pos == '-') {
            found_dash = true;
            pos++;
        } else if (*pos == ':' || *pos == ' ' || *pos == '\t') {
            pos++;
        } else {
            return false; // Invalid character
        }
    }

    // Valid if we found at least one complete column (ending with |) or column in progress
    return found_any_dash || found_dash;
}

// Parse table alignment specification
static char* parse_table_alignment(const char* line) {
    if (!line) return NULL;

    const char* pos = line;
    skip_whitespace(&pos);

    // Count columns first
    int column_count = 0;
    const char* temp_pos = pos;
    while (*temp_pos) {
        if (*temp_pos == '|') {
            column_count++;
        }
        temp_pos++;
    }

    if (column_count <= 1) return NULL;
    column_count--; // Subtract 1 because we count separators

    // Allocate alignment string
    char* alignment = (char*)malloc(column_count + 1);
    if (!alignment) return NULL;

    int col_index = 0;
    if (*pos == '|') pos++; // Skip leading |

    while (*pos && col_index < column_count) {
        // Find column boundaries
        const char* col_start = pos;
        while (*pos && *pos != '|') pos++;

        // Analyze this column for alignment
        bool left_colon = false;
        bool right_colon = false;

        // Check for colons at start and end
        const char* col_pos = col_start;
        skip_whitespace(&col_pos);
        if (*col_pos == ':') left_colon = true;

        const char* col_end = pos - 1;
        while (col_end > col_start && (*col_end == ' ' || *col_end == '\t')) col_end--;
        if (col_end >= col_start && *col_end == ':') right_colon = true;

        // Determine alignment
        if (left_colon && right_colon) {
            alignment[col_index] = 'c'; // center
        } else if (right_colon) {
            alignment[col_index] = 'r'; // right
        } else {
            alignment[col_index] = 'l'; // left (default)
        }

        col_index++;
        if (*pos == '|') pos++;
    }

    alignment[column_count] = '\0';
    return alignment;
}

// Apply table alignment to table element
static void apply_table_alignment(Element* table, const char* alignment_spec) {
    // Note: We need parser->input() to add attributes, so we'll handle this in the calling function
    // This function serves as a placeholder for potential future alignment processing
    (void)table;
    (void)alignment_spec;
}

// Check if line continues the table
static bool is_table_continuation(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Empty line ends table
    if (!*pos) return false;

    // Must contain pipe character to be table row
    return is_table_row(pos);
}

// Parse table cell content with enhanced formatting support
static Item parse_table_cell_content(MarkupParser* parser, const char* cell_text) {
    if (!cell_text || !*cell_text) {
        String* empty = create_string(parser, "");
        return (Item){.item = s2it(empty)};
    }

    // Trim whitespace
    while (*cell_text == ' ' || *cell_text == '\t') cell_text++;

    const char* end = cell_text + strlen(cell_text) - 1;
    while (end > cell_text && (*end == ' ' || *end == '\t')) end--;

    size_t len = end - cell_text + 1;
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) {
        String* empty = create_string(parser, "");
        return (Item){.item = s2it(empty)};
    }

    strncpy(trimmed, cell_text, len);
    trimmed[len] = '\0';

    // Parse inline content
    Item result = parse_inline_spans(parser, trimmed);
    free(trimmed);

    return result;
}

// Enhanced table row parsing (keep original function name for compatibility)
static Item parse_table_row(MarkupParser* parser, const char* line) {
    Element* row = create_element(parser, "tr");
    if (!row) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Split line by | characters
    const char* pos = line;
    skip_whitespace(&pos);

    // Skip leading | if present
    if (*pos == '|') pos++;

    while (*pos) {
        // Find next | or end of line
        const char* cell_start = pos;
        const char* cell_end = pos;

        while (*cell_end && *cell_end != '|') {
            cell_end++;
        }

        // Extract cell content
        size_t cell_len = cell_end - cell_start;
        char* cell_text = (char*)malloc(cell_len + 1);
        if (!cell_text) break;

        strncpy(cell_text, cell_start, cell_len);
        cell_text[cell_len] = '\0';

        // Create table cell
        Element* cell = create_element(parser, "td");
        if (cell) {
            // Parse cell content with enhanced formatting
            Item cell_content = parse_table_cell_content(parser, cell_text);
            if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                list_push((List*)cell, cell_content);
                increment_element_content_length(cell);
            }

            // Add cell to row
            list_push((List*)row, (Item){.item = (uint64_t)cell});
            increment_element_content_length(row);
        }

        free(cell_text);

        // Move to next cell
        pos = cell_end;
        if (*pos == '|') pos++;

        if (!*pos) break;
    }

    parser->current_line++;
    return (Item){.item = (uint64_t)row};
}

// Phase 2: Enhanced inline content parsing with spans
static Item parse_inline_spans(MarkupParser* parser, const char* text) {
    if (!text || !*text) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // For simple text without markup, return as string (with trailing spaces trimmed)
    // Include \n and \r to catch hard line breaks (2+ spaces before newline)
    if (!strpbrk(text, "*_`[!~\\$:^{@'<\n\r")) {
        // Trim trailing spaces from end of text
        size_t len = strlen(text);
        while (len > 0 && text[len - 1] == ' ') {
            len--;
        }
        String* content = parser->builder.createString(text, len);
        return (Item){.item = s2it(content)};
    }

    // Create span container for mixed inline content
    Element* span = create_element(parser, "span");
    if (!span) {
        String* content = create_string(parser, text);
        return (Item){.item = s2it(content)};
    }

    // Parse inline elements
    const char* pos = text;
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (*pos) {
        if (*pos == '*' || *pos == '_') {
            // Flush any accumulated text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            // Parse bold/italic
            const char* before_parse = pos;
            Item inline_item = parse_bold_italic(parser, &pos);
            if (inline_item.item != ITEM_ERROR && inline_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, inline_item);
                increment_element_content_length(span);
            } else {
                // parse_bold_italic failed - add the marker character(s) to buffer
                // pos was already advanced in parse_bold_italic
                while (before_parse < pos) {
                    stringbuf_append_char(sb, *before_parse);
                    before_parse++;
                }
            }
        }
        else if (*pos == '`') {
            // Flush text and parse code span
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item code_item = parse_code_span(parser, &pos);
            if (code_item.item != ITEM_ERROR && code_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, code_item);
                increment_element_content_length(span);
            }
        }
        else if (*pos == '[') {
            // Flush text first
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            // MediaWiki-specific link parsing
            if (parser->config.format == MARKUP_WIKI && *(pos+1) == '[') {
                Item wiki_link_item = parse_wiki_link(parser, &pos);
                if (wiki_link_item.item != ITEM_ERROR && wiki_link_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, wiki_link_item);
                    increment_element_content_length(span);
                    continue;
                }
            }
            // MediaWiki external link parsing
            if (parser->config.format == MARKUP_WIKI) {
                Item wiki_external_item = parse_wiki_external_link(parser, &pos);
                if (wiki_external_item.item != ITEM_ERROR && wiki_external_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, wiki_external_item);
                    increment_element_content_length(span);
                    continue;
                }
            }

            // Phase 6: Check for footnote reference [^1] first
            if (*(pos+1) == '^') {
                Item footnote_ref = parse_footnote_reference(parser, &pos);
                if (footnote_ref.item != ITEM_ERROR && footnote_ref.item != ITEM_UNDEFINED) {
                    list_push((List*)span, footnote_ref);
                    increment_element_content_length(span);
                }
            }
            // Phase 6: Check for citation [@key]
            else if (*(pos+1) == '@') {
                Item citation = parse_citation(parser, &pos);
                if (citation.item != ITEM_ERROR && citation.item != ITEM_UNDEFINED) {
                    list_push((List*)span, citation);
                    increment_element_content_length(span);
                }
            }
            // Regular link parsing
            else {
                Item link_item = parse_link(parser, &pos);
                if (link_item.item != ITEM_ERROR && link_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, link_item);
                    increment_element_content_length(span);
                }
            }
        }
        else if (*pos == '\'' && parser->config.format == MARKUP_WIKI) {
            // Flush text and parse MediaWiki bold/italic
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item wiki_format_item = parse_wiki_bold_italic(parser, &pos);
            if (wiki_format_item.item != ITEM_ERROR && wiki_format_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, wiki_format_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
            }
        }
        else if (*pos == '!' && *(pos+1) == '[') {
            // Flush text and parse image
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                String* key = create_string(parser, "content");
                // Use builder to add attribute to existing element
                parser->builder.putToElement(span, key, text_item);
                stringbuf_reset(sb);
            }

            Item image_item = parse_image(parser, &pos);
            if (image_item.item != ITEM_ERROR && image_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, image_item);
                increment_element_content_length(span);
            }
        }
        // Phase 4: Enhanced inline parsing
        else if (*pos == '~' && *(pos+1) == '~') {
            // Flush text and parse strikethrough
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item strikethrough_item = parse_strikethrough(parser, &pos);
            if (strikethrough_item.item != ITEM_ERROR && strikethrough_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, strikethrough_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
            }
        }
        else if (*pos == '^') {
            // Flush text and parse superscript
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item superscript_item = parse_superscript(parser, &pos);
            if (superscript_item.item != ITEM_ERROR && superscript_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, superscript_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
            }
        }
        else if (*pos == '~' && *(pos+1) != '~') {
            // Flush text and parse subscript
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item subscript_item = parse_subscript(parser, &pos);
            if (subscript_item.item != ITEM_ERROR && subscript_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, subscript_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
            }
        }
        else if (*pos == ':') {
            // Check if this might be an ASCII math prefix before flushing text
            const char* lookahead_pos = pos;
            bool is_ascii_math_prefix = false;

            // Check if we have "asciimath::" or "AM::" starting from the text buffer + current position
            if (sb->str && sb->length >= 9 &&
                strncmp(sb->str->chars + sb->length - 9, "asciimath", 9) == 0 &&
                strncmp(pos, "::", 2) == 0) {
                is_ascii_math_prefix = true;
            } else if (sb->str && sb->length >= 2 &&
                       strncmp(sb->str->chars + sb->length - 2, "AM", 2) == 0 &&
                       strncmp(pos, "::", 2) == 0) {
                is_ascii_math_prefix = true;
            }

            if (is_ascii_math_prefix) {
                // Extract the prefix length
                size_t prefix_len = (sb->length >= 9 &&
                                   strncmp(sb->str->chars + sb->length - 9, "asciimath", 9) == 0) ? 9 : 2;

                // Create a temporary buffer with the content before the prefix
                size_t before_prefix_len = sb->length - prefix_len;

                // Flush any text before the prefix
                if (before_prefix_len > 0) {
                    // Create a string with content before the prefix
                    char* before_prefix = (char*)malloc(before_prefix_len + 1);
                    if (before_prefix) {
                        strncpy(before_prefix, sb->str->chars, before_prefix_len);
                        before_prefix[before_prefix_len] = '\0';

                        String* text_content = create_string(parser, before_prefix);
                        if (text_content) {
                            Item text_item = {.item = s2it(text_content)};
                            list_push((List*)span, text_item);
                            increment_element_content_length(span);
                        }
                        free(before_prefix);
                    }
                }

                // Reset string buffer
                stringbuf_reset(sb);

                // Parse ASCII math starting from the prefix in the original text
                // We need to back up the position to the start of the prefix
                const char* prefix_start = pos - 1; // Back up to the first ':'
                // Back up further to the start of "asciimath" or "AM"
                prefix_start -= (prefix_len - 1);
                const char* parse_pos = prefix_start;

                // Try to parse ASCII math prefix
                Item ascii_math_item = parse_ascii_math_prefix(parser, &parse_pos);
                if (ascii_math_item.item != ITEM_ERROR && ascii_math_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, ascii_math_item);
                    increment_element_content_length(span);
                    pos = parse_pos; // Update position to where parsing ended
                } else {
                    // Parsing failed, add the prefix back to buffer
                    stringbuf_append_str(sb, (prefix_len == 9) ? "asciimath" : "AM");
                    stringbuf_append_char(sb, ':');
                    pos++;
                }
            } else {
                // Regular colon handling - flush text and try emoji shortcode
                if (sb->length > 0) {
                    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                    Item text_item = {.item = s2it(text_content)};
                    list_push((List*)span, text_item);
                    increment_element_content_length(span);
                    stringbuf_reset(sb);
                }

                const char* old_pos = pos;

                // Try emoji shortcode
                Item emoji_item = parse_emoji_shortcode(parser, &pos);
                if (emoji_item.item != ITEM_ERROR && emoji_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, emoji_item);
                    increment_element_content_length(span);
                } else if (pos == old_pos) {
                    // Parse failed and didn't advance, add the colon to text buffer and advance
                    stringbuf_append_char(sb, ':');
                    pos++;
                }
            }
        }
        // Phase 6: Wiki template parsing {{template|args}}
        else if (*pos == '{' && *(pos+1) == '{') {
            // Flush text and parse wiki template
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item template_item = parse_wiki_template(parser, &pos);
            if (template_item.item != ITEM_ERROR && template_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, template_item);
                increment_element_content_length(span);
            }
        }
        else if (*pos == '$') {
            // Flush text and parse inline math
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item math_item = parse_inline_math(parser, &pos);
            if (math_item.item != ITEM_ERROR && math_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, math_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance - add $ to buffer literally
                stringbuf_append_char(sb, *pos);
                pos++;
            }
        }
        // CommonMark §2.4: Backslash escapes
        else if (*pos == '\\') {
            char next = *(pos+1);

            // Hard line break: backslash at end of line
            if (next == '\n' || next == '\r') {
                // Flush accumulated text
                if (sb->length > 0) {
                    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                    Item text_item = {.item = s2it(text_content)};
                    list_push((List*)span, text_item);
                    increment_element_content_length(span);
                    stringbuf_reset(sb);
                }

                // Create <br> element for hard line break
                Element* br = create_element(parser, "br");
                if (br) {
                    list_push((List*)span, (Item){.item = (uint64_t)br});
                    increment_element_content_length(span);
                }

                pos += 2;
                // Skip CRLF or LFCR combinations
                if ((next == '\r' && *pos == '\n') || (next == '\n' && *pos == '\r')) {
                    pos++;
                }
            }
            // Escapable ASCII punctuation: strip backslash, keep character
            else if (next && strchr("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", next)) {
                pos++; // Skip backslash
                stringbuf_append_char(sb, *pos);
                pos++;
            }
            // Not an escape sequence: treat backslash as literal
            else {
                stringbuf_append_char(sb, *pos);
                pos++;
            }
        }
        else if (*pos == '<') {
            // Flush text first
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            // Try to parse as raw HTML tag
            const char* old_pos = pos;
            Item html_item = parse_raw_html_tag(parser, &pos);
            if (html_item.item != ITEM_ERROR && html_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, html_item);
                increment_element_content_length(span);
            } else {
                // Not a valid HTML tag - add < literally
                stringbuf_append_char(sb, '<');
                pos = old_pos + 1;
            }
        }
        else if (*pos == '\n' || *pos == '\r') {
            // Check for hard line break (2+ spaces before newline)
            // Look back in the string buffer for trailing spaces
            int trailing_spaces = 0;
            if (sb->length > 0) {
                const char* buf = sb->str->chars;
                int i = sb->length - 1;
                while (i >= 0 && buf[i] == ' ') {
                    trailing_spaces++;
                    i--;
                }
            }

            if (trailing_spaces >= 2) {
                // Hard line break - trim trailing spaces from buffer
                sb->length -= trailing_spaces;
                sb->str->chars[sb->length] = '\0';

                // Flush accumulated text
                if (sb->length > 0) {
                    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                    Item text_item = {.item = s2it(text_content)};
                    list_push((List*)span, text_item);
                    increment_element_content_length(span);
                    stringbuf_reset(sb);
                }

                // Create <br> element for hard line break
                Element* br = create_element(parser, "br");
                if (br) {
                    list_push((List*)span, (Item){.item = (uint64_t)br});
                    increment_element_content_length(span);
                }

                // Skip the newline
                if (*pos == '\r' && pos[1] == '\n') {
                    pos += 2;
                } else if (*pos == '\n' && pos[1] == '\r') {
                    pos += 2;
                } else {
                    pos++;
                }

                // Skip leading spaces on the next line
                while (*pos == ' ') pos++;
            } else {
                // Soft line break - add newline character to buffer
                stringbuf_append_char(sb, *pos);
                pos++;
            }
        }
        else {
            // Regular character, add to text buffer
            stringbuf_append_char(sb, *pos);
            pos++;
        }
    }

    // Flush any remaining text (trim trailing whitespace for CommonMark)
    if (sb->length > 0) {
        // Trim trailing spaces from end of paragraph
        while (sb->length > 0 && sb->str->chars[sb->length - 1] == ' ') {
            sb->length--;
        }
        sb->str->chars[sb->length] = '\0';

        if (sb->length > 0) {
            String* text_content = parser->builder.createString(sb->str->chars, sb->length);
            Item text_item = {.item = s2it(text_content)};
            list_push((List*)span, text_item);
            increment_element_content_length(span);
        }
    }

    return (Item){.item = (uint64_t)span};
}// Parse inline content with format-specific enhancements
static Item parse_inline_content(MarkupParser* parser, const char* text) {
    if (!text || !*text) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // For RST format, handle RST-specific inline elements first
    if (parser->config.format == MARKUP_RST) {
        // Check for double backticks literal text
        if (text[0] == '`' && text[1] == '`') {
            const char* pos = text;
            Item rst_literal = parse_rst_double_backtick_literal(parser, &pos);
            if (rst_literal.item != ITEM_UNDEFINED) {
                // If there's more text after the literal, create a span
                if (*pos) {
                    Element* span = create_element(parser, "span");
                    if (span) {
                        list_push((List*)span, rst_literal);
                        increment_element_content_length(span);

                        Item remaining = parse_inline_content(parser, pos);
                        if (remaining.item != ITEM_UNDEFINED) {
                            list_push((List*)span, remaining);
                            increment_element_content_length(span);
                        }

                        return (Item){.item = (uint64_t)span};
                    }
                }
                return rst_literal;
            }
        }

        // Check for trailing underscore references
        const char* underscore_pos = strchr(text, '_');
        if (underscore_pos && underscore_pos > text) {
            // Parse up to the underscore normally, then handle the reference
            size_t prefix_len = underscore_pos - text;
            char* prefix = (char*)malloc(prefix_len + 1);
            if (prefix) {
                strncpy(prefix, text, prefix_len);
                prefix[prefix_len] = '\0';

                Element* span = create_element(parser, "span");
                if (span) {
                    // Parse prefix normally
                    if (strlen(prefix) > 0) {
                        Item prefix_item = parse_inline_spans(parser, prefix);
                        if (prefix_item.item != ITEM_UNDEFINED) {
                            list_push((List*)span, prefix_item);
                            increment_element_content_length(span);
                        }
                    }

                    // Parse the reference
                    const char* ref_pos = underscore_pos;
                    Item ref_item = parse_rst_trailing_underscore_reference(parser, &ref_pos);
                    if (ref_item.item != ITEM_UNDEFINED) {
                        list_push((List*)span, ref_item);
                        increment_element_content_length(span);
                    }

                    // Parse any remaining text
                    if (*ref_pos) {
                        Item remaining = parse_inline_content(parser, ref_pos);
                        if (remaining.item != ITEM_UNDEFINED) {
                            list_push((List*)span, remaining);
                            increment_element_content_length(span);
                        }
                    }

                    free(prefix);
                    return (Item){.item = (uint64_t)span};
                }
                free(prefix);
            }
        }
    }

    // For AsciiDoc format, handle AsciiDoc-specific inline elements
    if (parser->config.format == MARKUP_ASCIIDOC) {
        return parse_asciidoc_inline(parser, text);
    }

    // Default to standard inline spans parsing
    return parse_inline_spans(parser, text);
}

// CommonMark flanking delimiter helpers
static inline bool is_cm_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || c == '\0';
}

static inline bool is_cm_punctuation(char c) {
    // ASCII punctuation characters
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
           (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

/**
 * Check if delimiter run at position is left-flanking
 * A left-flanking delimiter run:
 * (1) is not followed by Unicode whitespace, AND
 * (2a) not followed by punctuation, OR
 * (2b) followed by punctuation AND preceded by whitespace or punctuation
 */
static bool is_left_flanking(const char* full_text, const char* delim_start, int delim_len) {
    char before = (delim_start > full_text) ? *(delim_start - 1) : ' ';
    char after = *(delim_start + delim_len);

    // (1) not followed by whitespace
    if (is_cm_whitespace(after)) return false;

    // (2a) not followed by punctuation
    if (!is_cm_punctuation(after)) return true;

    // (2b) followed by punctuation, check if preceded by whitespace or punctuation
    return is_cm_whitespace(before) || is_cm_punctuation(before);
}

/**
 * Check if delimiter run at position is right-flanking
 * A right-flanking delimiter run:
 * (1) is not preceded by Unicode whitespace, AND
 * (2a) not preceded by punctuation, OR
 * (2b) preceded by punctuation AND followed by whitespace or punctuation
 */
static bool is_right_flanking(const char* full_text, const char* delim_start, int delim_len) {
    char before = (delim_start > full_text) ? *(delim_start - 1) : ' ';
    char after = *(delim_start + delim_len);

    // (1) not preceded by whitespace
    if (is_cm_whitespace(before)) return false;

    // (2a) not preceded by punctuation
    if (!is_cm_punctuation(before)) return true;

    // (2b) preceded by punctuation, check if followed by whitespace or punctuation
    return is_cm_whitespace(after) || is_cm_punctuation(after);
}

/**
 * Check if delimiter can open emphasis
 * For *: can open if left-flanking
 * For _: can open if left-flanking AND (not right-flanking OR preceded by punctuation)
 */
static bool can_open_emphasis(const char* full_text, const char* delim_start, int delim_len, char marker) {
    bool left = is_left_flanking(full_text, delim_start, delim_len);
    if (!left) return false;

    if (marker == '*') return true;

    // For underscore
    bool right = is_right_flanking(full_text, delim_start, delim_len);
    if (!right) return true;

    char before = (delim_start > full_text) ? *(delim_start - 1) : ' ';
    return is_cm_punctuation(before);
}

/**
 * Check if delimiter can close emphasis
 * For *: can close if right-flanking
 * For _: can close if right-flanking AND (not left-flanking OR followed by punctuation)
 */
static bool can_close_emphasis(const char* full_text, const char* delim_start, int delim_len, char marker) {
    bool right = is_right_flanking(full_text, delim_start, delim_len);
    if (!right) return false;

    if (marker == '*') return true;

    // For underscore
    bool left = is_left_flanking(full_text, delim_start, delim_len);
    if (!left) return true;

    char after = *(delim_start + delim_len);
    return is_cm_punctuation(after);
}

// Parse bold and italic text (**bold**, *italic*, __bold__, _italic_)
static Item parse_bold_italic(MarkupParser* parser, const char** text) {
    // We need context from parse_inline_spans to properly check flanking
    // This function is called when we see * or _

    const char* pos = *text;
    char marker = *pos;
    int count = 0;

    // Count consecutive markers
    while (*pos == marker) {
        count++;
        pos++;
    }

    if (count == 0) {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Check if this delimiter can open emphasis
    // We need the full text context - for now use a simple heuristic
    // based on character after the delimiter run
    char after = *pos;

    // Simple left-flanking check: not followed by whitespace
    if (is_cm_whitespace(after)) {
        // Not left-flanking, can't open - treat as literal
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    // For punctuation after delimiter, need to check before too
    // But we don't have full text context here, so be lenient

    // Find closing markers that can close emphasis
    const char* content_start = pos;
    const char* end = NULL;
    int end_count = 0;

    while (*pos) {
        if (*pos == marker) {
            const char* marker_start = pos;
            int marker_count = 0;
            while (*pos == marker) {
                marker_count++;
                pos++;
            }

            // Check if this can close emphasis
            // Simple right-flanking check: not preceded by whitespace
            char before_close = (marker_start > content_start) ? *(marker_start - 1) : ' ';
            char after_close = *pos;

            bool can_close = !is_cm_whitespace(before_close);

            // For underscore, additional rule: can't be intraword
            if (marker == '_' && can_close) {
                // If preceded by alphanumeric and followed by alphanumeric, can't close
                if (!is_cm_whitespace(before_close) && !is_cm_punctuation(before_close) &&
                    !is_cm_whitespace(after_close) && !is_cm_punctuation(after_close) && after_close != '\0') {
                    can_close = false;
                }
            }

            if (can_close && marker_count >= count) {
                end = marker_start;
                end_count = marker_count;
                break;
            }
        } else {
            pos++;
        }
    }

    if (!end) {
        // No closing marker found, treat as plain text
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Determine how many markers to consume
    // We match the minimum of opening and closing count
    int match_count = (count < end_count) ? count : end_count;

    // Create appropriate element based on match_count
    Element* elem;
    if (match_count >= 3) {
        // Create strong with em inside for ***
        elem = create_element(parser, "strong");
        Element* em_elem = create_element(parser, "em");

        if (!elem || !em_elem) {
            *text = end + match_count;
            return (Item){.item = ITEM_ERROR};
        }

        // Extract content between markers
        size_t content_len = end - content_start;
        char* content = (char*)malloc(content_len + 1);
        if (content) {
            strncpy(content, content_start, content_len);
            content[content_len] = '\0';

            Item inner_content = parse_inline_spans(parser, content);
            if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
                list_push((List*)em_elem, inner_content);
                increment_element_content_length(em_elem);
            }
            free(content);
        }

        list_push((List*)elem, (Item){.item = (uint64_t)em_elem});
        increment_element_content_length(elem);

        *text = end + match_count;
        return (Item){.item = (uint64_t)elem};
    }
    else if (match_count == 2) {
        elem = create_element(parser, "strong");
    } else {
        elem = create_element(parser, "em");
    }

    if (!elem) {
        *text = end + match_count;
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content between markers
    size_t content_len = end - content_start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, content_start, content_len);
        content[content_len] = '\0';

        // Recursively parse inline content
        Item inner_content = parse_inline_spans(parser, content);
        if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
            list_push((List*)elem, inner_content);
            increment_element_content_length(elem);
        }

        free(content);
    }

    *text = end + match_count;  // Move past closing markers
    return (Item){.item = (uint64_t)elem};
}

// Parse code spans (`code`, ``code``)
static Item parse_code_span(MarkupParser* parser, const char** text) {
    const char* start = *text;
    int backticks = 0;

    // Count opening backticks
    while (*start == '`') {
        backticks++;
        start++;
    }

    // Find matching closing backticks
    const char* pos = start;
    const char* end = NULL;

    while (*pos) {
        if (*pos == '`') {
            const char* close_start = pos;
            int close_count = 0;
            while (*pos == '`') {
                close_count++;
                pos++;
            }

            if (close_count == backticks) {
                end = close_start;
                break;
            }
        } else {
            pos++;
        }
    }

    if (!end) {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* code = create_element(parser, "code");
    if (!code) {
        *text = end + backticks;
        return (Item){.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, code, "type", "inline");

    // Extract code content - convert line endings to spaces per CommonMark
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        size_t j = 0;
        for (size_t i = 0; i < content_len; i++) {
            if (start[i] == '\n' || start[i] == '\r') {
                // Convert line ending to space
                if (start[i] == '\r' && i + 1 < content_len && start[i+1] == '\n') {
                    i++; // Skip \n after \r
                }
                content[j++] = ' ';
            } else {
                content[j++] = start[i];
            }
        }
        content[j] = '\0';
        content_len = j;

        // CommonMark: If the resulting string both begins AND ends with a space,
        // but does not consist entirely of space characters, remove one space from front and back
        if (content_len >= 2 && content[0] == ' ' && content[content_len - 1] == ' ') {
            // Check if not all spaces
            bool all_spaces = true;
            for (size_t i = 0; i < content_len; i++) {
                if (content[i] != ' ') {
                    all_spaces = false;
                    break;
                }
            }
            if (!all_spaces) {
                // Remove one space from front and back
                memmove(content, content + 1, content_len - 1);
                content_len -= 2;
                content[content_len] = '\0';
            }
        }

        String* code_text = parser->builder.createString(content, content_len);
        Item code_item = {.item = s2it(code_text)};
        list_push((List*)code, code_item);
        increment_element_content_length(code);

        free(content);
    }

    *text = end + backticks;
    return (Item){.item = (uint64_t)code};
}

// Helper: check if character is valid for start of tag name (ASCII letter)
static inline bool is_tag_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Helper: check if character is valid in tag name (letter, digit, hyphen)
static inline bool is_tag_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-';
}

// Helper: check if character is valid for start of attribute name
static inline bool is_attr_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}

// Helper: check if character is valid in attribute name
static inline bool is_attr_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':' || c == '-';
}

// Parse raw HTML tag - returns raw-html element containing the tag text
static Item parse_raw_html_tag(MarkupParser* parser, const char** text) {
    const char* start = *text;
    const char* pos = start;

    if (*pos != '<') {
        return (Item){.item = ITEM_UNDEFINED};
    }
    pos++;

    // Check for autolink (URI scheme or email) before HTML
    // Autolink URI: <scheme:...> where scheme is [a-zA-Z][a-zA-Z0-9+.-]{0,31}
    {
        const char* scheme_start = pos;
        if ((*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z')) {
            pos++;
            int scheme_len = 1;
            while (scheme_len < 32 && ((*pos >= 'a' && *pos <= 'z') ||
                   (*pos >= 'A' && *pos <= 'Z') || (*pos >= '0' && *pos <= '9') ||
                   *pos == '+' || *pos == '.' || *pos == '-')) {
                pos++;
                scheme_len++;
            }
            if (*pos == ':') {
                // Found valid scheme, now find > ensuring no whitespace or <
                pos++;
                const char* uri_start = scheme_start;
                while (*pos && *pos != '>' && *pos != '<' && *pos != ' ' &&
                       *pos != '\t' && *pos != '\n' && *pos != '\r') {
                    pos++;
                }
                if (*pos == '>') {
                    // Valid autolink URI
                    size_t uri_len = pos - uri_start;
                    pos++; // Skip >
                    char uri_buf[1024];
                    if (uri_len < sizeof(uri_buf)) {
                        memcpy(uri_buf, uri_start, uri_len);
                        uri_buf[uri_len] = '\0';
                        Element* link = create_element(parser, "a");
                        if (link) {
                            add_attribute_to_element(parser, link, "href", uri_buf);
                            // Text content is the URI
                            String* text_str = parser->builder.createString(uri_start, uri_len);
                            list_push((List*)link, (Item){.item = s2it(text_str)});
                            increment_element_content_length(link);
                            *text = pos;
                            return (Item){.item = (uint64_t)link};
                        }
                    }
                }
            }
        }
        pos = start + 1; // Reset for other checks
    }

    // Check for email autolink: <addr@domain>
    {
        const char* email_start = pos;
        // Basic email validation - at least one char, @, then domain
        bool has_at = false;
        bool valid_email = true;
        const char* at_pos = NULL;
        while (*pos && *pos != '>' && *pos != '<' && *pos != ' ' &&
               *pos != '\t' && *pos != '\n' && *pos != '\r') {
            if (*pos == '@') {
                if (has_at) {
                    valid_email = false;
                    break;
                }
                if (pos == email_start) {
                    valid_email = false;
                    break;
                }
                has_at = true;
                at_pos = pos;
            }
            pos++;
        }
        if (*pos == '>' && has_at && valid_email && at_pos &&
            pos > at_pos + 1) {  // Must have domain after @
            size_t email_len = pos - email_start;
            pos++; // Skip >

            // Build mailto: href
            char mailto_buf[512];
            if (email_len < sizeof(mailto_buf) - 8) {
                memcpy(mailto_buf, "mailto:", 7);
                memcpy(mailto_buf + 7, email_start, email_len);
                mailto_buf[7 + email_len] = '\0';

                Element* link = create_element(parser, "a");
                if (link) {
                    add_attribute_to_element(parser, link, "href", mailto_buf);
                    String* email_str = parser->builder.createString(email_start, email_len);
                    list_push((List*)link, (Item){.item = s2it(email_str)});
                    increment_element_content_length(link);
                    *text = pos;
                    return (Item){.item = (uint64_t)link};
                }
            }
        }
        pos = start + 1; // Reset for HTML checks
    }

    // HTML Comment: <!-- ... -->
    if (pos[0] == '!' && pos[1] == '-' && pos[2] == '-') {
        pos += 3;
        // Find -->
        while (*pos && !(pos[0] == '-' && pos[1] == '-' && pos[2] == '>')) {
            pos++;
        }
        if (pos[0] == '-' && pos[1] == '-' && pos[2] == '>') {
            pos += 3;
            size_t len = pos - start;
            String* html_str = parser->builder.createString(start, len);
            Element* elem = create_element(parser, "raw-html");
            if (elem && html_str) {
                list_push((List*)elem, (Item){.item = s2it(html_str)});
                increment_element_content_length(elem);
                *text = pos;
                return (Item){.item = (uint64_t)elem};
            }
        }
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Processing instruction: <? ... ?>
    if (*pos == '?') {
        pos++;
        while (*pos && !(pos[0] == '?' && pos[1] == '>')) {
            pos++;
        }
        if (pos[0] == '?' && pos[1] == '>') {
            pos += 2;
            size_t len = pos - start;
            String* html_str = parser->builder.createString(start, len);
            Element* elem = create_element(parser, "raw-html");
            if (elem && html_str) {
                list_push((List*)elem, (Item){.item = s2it(html_str)});
                increment_element_content_length(elem);
                *text = pos;
                return (Item){.item = (uint64_t)elem};
            }
        }
        return (Item){.item = ITEM_UNDEFINED};
    }

    // CDATA: <![CDATA[ ... ]]>
    if (pos[0] == '!' && pos[1] == '[' && strncmp(pos, "![CDATA[", 8) == 0) {
        pos += 8;
        while (*pos && !(pos[0] == ']' && pos[1] == ']' && pos[2] == '>')) {
            pos++;
        }
        if (pos[0] == ']' && pos[1] == ']' && pos[2] == '>') {
            pos += 3;
            size_t len = pos - start;
            String* html_str = parser->builder.createString(start, len);
            Element* elem = create_element(parser, "raw-html");
            if (elem && html_str) {
                list_push((List*)elem, (Item){.item = s2it(html_str)});
                increment_element_content_length(elem);
                *text = pos;
                return (Item){.item = (uint64_t)elem};
            }
        }
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Declaration: <! followed by letter, ends with >
    if (*pos == '!' && is_tag_name_start(pos[1])) {
        pos += 2;
        while (*pos && *pos != '>') {
            pos++;
        }
        if (*pos == '>') {
            pos++;
            size_t len = pos - start;
            String* html_str = parser->builder.createString(start, len);
            Element* elem = create_element(parser, "raw-html");
            if (elem && html_str) {
                list_push((List*)elem, (Item){.item = s2it(html_str)});
                increment_element_content_length(elem);
                *text = pos;
                return (Item){.item = (uint64_t)elem};
            }
        }
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Close tag: </tagname>
    if (*pos == '/') {
        pos++;
        if (!is_tag_name_start(*pos)) {
            return (Item){.item = ITEM_UNDEFINED};
        }
        while (is_tag_name_char(*pos)) pos++;
        // Skip whitespace
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
        if (*pos != '>') {
            return (Item){.item = ITEM_UNDEFINED};
        }
        pos++;
        size_t len = pos - start;
        String* html_str = parser->builder.createString(start, len);
        Element* elem = create_element(parser, "raw-html");
        if (elem && html_str) {
            list_push((List*)elem, (Item){.item = s2it(html_str)});
            increment_element_content_length(elem);
            *text = pos;
            return (Item){.item = (uint64_t)elem};
        }
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Open tag: <tagname ...>
    if (!is_tag_name_start(*pos)) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Parse tag name
    while (is_tag_name_char(*pos)) pos++;

    // Parse attributes (simplified)
    while (*pos) {
        // Skip whitespace
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

        // End of tag?
        if (*pos == '>') {
            pos++;
            break;
        }
        if (*pos == '/' && pos[1] == '>') {
            pos += 2;
            break;
        }

        // Attribute name
        if (!is_attr_name_start(*pos)) {
            // Invalid attribute start
            return (Item){.item = ITEM_UNDEFINED};
        }
        while (is_attr_name_char(*pos)) pos++;

        // Skip whitespace
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

        // Optional = followed by value
        if (*pos == '=') {
            pos++;
            // Skip whitespace
            while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

            // Attribute value
            if (*pos == '"') {
                pos++;
                while (*pos && *pos != '"') pos++;
                if (*pos == '"') pos++;
                else return (Item){.item = ITEM_UNDEFINED};
            } else if (*pos == '\'') {
                pos++;
                while (*pos && *pos != '\'') pos++;
                if (*pos == '\'') pos++;
                else return (Item){.item = ITEM_UNDEFINED};
            } else {
                // Unquoted value - ends at whitespace or >
                while (*pos && *pos != ' ' && *pos != '\t' && *pos != '\n' &&
                       *pos != '\r' && *pos != '>' && *pos != '"' && *pos != '\'' &&
                       *pos != '=' && *pos != '<' && *pos != '`') {
                    pos++;
                }
            }
        }
    }

    // If we reached here, we have a valid tag
    if (pos > start + 1) {  // At least <x>
        size_t len = pos - start;
        String* html_str = parser->builder.createString(start, len);
        Element* elem = create_element(parser, "raw-html");
        if (elem && html_str) {
            list_push((List*)elem, (Item){.item = s2it(html_str)});
            increment_element_content_length(elem);
            *text = pos;
            return (Item){.item = (uint64_t)elem};
        }
    }

    return (Item){.item = ITEM_UNDEFINED};
}

// Parse links ([text](url))
static Item parse_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (*pos != '[') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    pos++; // Skip [

    // Find closing ] - handle nested brackets and backslash escapes
    const char* text_start = pos;
    const char* text_end = NULL;
    int bracket_depth = 1;

    while (*pos && bracket_depth > 0) {
        if (*pos == '\\' && *(pos+1)) {
            pos += 2; // Skip escaped character
            continue;
        }
        if (*pos == '[') bracket_depth++;
        else if (*pos == ']') bracket_depth--;

        if (bracket_depth == 0) {
            text_end = pos;
        } else {
            pos++;
        }
    }
    if (bracket_depth == 0) pos++; // move past ]

    if (!text_end || *pos != '(') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    pos++; // Skip (

    // Skip leading whitespace in destination
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    // Parse URL - check for angle bracket form
    const char* url_start = pos;
    const char* url_end = NULL;
    bool angle_bracket = (*pos == '<');

    if (angle_bracket) {
        pos++; // Skip <
        url_start = pos;
        while (*pos && *pos != '>' && *pos != '\n' && *pos != '\r') {
            pos++;
        }
        if (*pos != '>') {
            (*text)++;
            return (Item){.item = ITEM_UNDEFINED};
        }
        url_end = pos;
        pos++; // Skip >
    } else {
        // No angle brackets - find end of URL
        // URL ends at whitespace or ) with balanced parentheses
        int paren_count = 0;
        while (*pos) {
            if (*pos == '\\' && *(pos+1)) {
                pos += 2;
                continue;
            }
            if (*pos == '(') {
                paren_count++;
                pos++;
            } else if (*pos == ')') {
                if (paren_count == 0) break;
                paren_count--;
                pos++;
            } else if (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') {
                break;
            } else {
                pos++;
            }
        }
        url_end = pos;
    }

    // Skip whitespace between URL and title
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    // Parse optional title
    const char* title_start = NULL;
    const char* title_end = NULL;
    char title_delim = 0;

    if (*pos == '"' || *pos == '\'' || *pos == '(') {
        title_delim = *pos;
        if (title_delim == '(') title_delim = ')';
        pos++; // Skip opening quote
        title_start = pos;

        while (*pos && *pos != title_delim) {
            if (*pos == '\\' && *(pos+1)) {
                pos += 2;
                continue;
            }
            pos++;
        }
        if (*pos == title_delim) {
            title_end = pos;
            pos++; // Skip closing quote
        }
    }

    // Skip trailing whitespace
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    // Must end with )
    if (*pos != ')') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }
    pos++; // Skip )

    Element* link = create_element(parser, "a");
    if (!link) {
        *text = pos;
        return (Item){.item = ITEM_ERROR};
    }

    // Add URL attribute (even if empty)
    size_t url_len = url_end - url_start;
    if (url_len > 0) {
        char* url = (char*)malloc(url_len + 1);
        if (url) {
            strncpy(url, url_start, url_len);
            url[url_len] = '\0';
            add_attribute_to_element(parser, link, "href", url);
            free(url);
        }
    } else {
        // Empty URL - still set href attribute to empty string
        add_attribute_to_element(parser, link, "href", "");
    }

    // Add title if present
    if (title_start && title_end && title_end > title_start) {
        size_t title_len = title_end - title_start;
        char* title = (char*)malloc(title_len + 1);
        if (title) {
            strncpy(title, title_start, title_len);
            title[title_len] = '\0';
            add_attribute_to_element(parser, link, "title", title);
            free(title);
        }
    }

    // Add link text content
    size_t text_len = text_end - text_start;
    char* link_text = (char*)malloc(text_len + 1);
    if (link_text) {
        strncpy(link_text, text_start, text_len);
        link_text[text_len] = '\0';

        // Parse inline content recursively
        Item inner_content = parse_inline_spans(parser, link_text);
        if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
            list_push((List*)link, inner_content);
            increment_element_content_length(link);
        }

        free(link_text);
    }

    *text = pos;
    return (Item){.item = (uint64_t)link};
}

// Parse images (![alt](src))
static Item parse_image(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (*pos != '!' || *(pos+1) != '[') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip ![

    // Find closing ]
    const char* alt_start = pos;
    const char* alt_end = NULL;

    while (*pos && *pos != ']') {
        pos++;
    }

    if (*pos != ']' || *(pos+1) != '(') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    alt_end = pos;
    pos += 2; // Skip ](

    // Find closing )
    const char* src_start = pos;
    const char* src_end = NULL;

    while (*pos && *pos != ')') {
        pos++;
    }

    if (*pos != ')') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }

    src_end = pos;
    pos++; // Skip )

    Element* img = create_element(parser, "img");
    if (!img) {
        *text = pos;
        return (Item){.item = ITEM_ERROR};
    }

    // Add src attribute
    size_t src_len = src_end - src_start;
    char* src = (char*)malloc(src_len + 1);
    if (src) {
        strncpy(src, src_start, src_len);
        src[src_len] = '\0';
        add_attribute_to_element(parser, img, "src", src);
        free(src);
    }

    // Add alt attribute
    size_t alt_len = alt_end - alt_start;
    char* alt = (char*)malloc(alt_len + 1);
    if (alt) {
        strncpy(alt, alt_start, alt_len);
        alt[alt_len] = '\0';
        add_attribute_to_element(parser, img, "alt", alt);
        free(alt);
    }

    *text = pos;
    return (Item){.item = (uint64_t)img};
}

// Input integration - main entry point from input system
Item input_markup(Input *input, const char* content) {
    if (!input || !content) {
        return (Item){.item = ITEM_ERROR};
    }

    // Extract filename from URL if available for format detection
    const char* filename = NULL;
    if (input->url) {
        Url* url = (Url*)input->url;
        if (url->pathname && url->pathname->chars && url->pathname->len > 0) {
            // Extract just the filename from the path
            const char* path_start = url->pathname->chars;
            size_t path_len = url->pathname->len;
            const char* last_slash = NULL;
            for (size_t i = 0; i < path_len; i++) {
                if (path_start[i] == '/') {
                    last_slash = path_start + i + 1;
                }
            }
            filename = last_slash ? last_slash : path_start;
        }
    }

    // Detect format using both content and filename
    MarkupFormat format = detect_markup_format(content, filename);
    const char* flavor = detect_markup_flavor(format, content);

    // Create parser configuration
    ParseConfig config = {
        .format = format,
        .flavor = flavor,
        .strict_mode = false
    };

    // Create markup parser directly - it extends InputContext
    MarkupParser parser(input, config);

    // Parse content
    Item result = parser.parseContent(content);

    if (result.item == ITEM_ERROR) {
        parser.addWarning(parser.tracker.location(), "Markup parsing returned error");
    }

    if (parser.hasErrors()) {
        // errors occurred during parsing
    }

    return result;
}

// Overloaded version that accepts explicit format
Item input_markup_with_format(Input *input, const char* content, MarkupFormat format) {
    if (!input || !content) {
        return (Item){.item = ITEM_ERROR};
    }

    const char* flavor = detect_markup_flavor(format, content);

    // Create parser configuration
    ParseConfig config = {
        .format = format,
        .flavor = flavor,
        .strict_mode = false
    };

    // Create markup parser directly - it extends InputContext
    MarkupParser parser(input, config);

    // Parse content
    Item result = parser.parseContent(content);

    if (result.item == ITEM_ERROR) {
        parser.addWarning(parser.tracker.location(), "Markup parsing with explicit format returned error");
    }

    if (parser.hasErrors()) {
        // errors occurred during parsing
    }

    return result;
}

// Math parser integration functions
static Item parse_math_content(InputContext& ctx, const char* math_content, const char* flavor) {
    Input* input = ctx.input();

    if (!input || !math_content) {
        return (Item){.item = ITEM_ERROR};
    }

    // Create a temporary Input to preserve the current state
    Item original_root = input->root;

    // Parse the math expression using the existing parse_math function
    // This modifies input->root, so we need to capture the result
    parse_math(input, math_content, flavor);
    Item result = input->root;

    // Restore original state
    input->root = original_root;

    return result;
}

static const char* detect_math_flavor(const char* content) {
    if (!content) return "latex";

    // Look for ASCII math indicators - must be specific ASCII math syntax patterns
    // NOTE: ^ alone is NOT an ASCII math indicator since it's valid LaTeX too
    // Only use clear ASCII math patterns that wouldn't appear in LaTeX
    bool has_ascii_indicators = (
        // Clear ASCII math function syntax
        (strstr(content, "sqrt(") != NULL) ||                // Function style sqrt
        (strstr(content, "sum_(") != NULL) ||                // ASCII summation
        (strstr(content, "prod_(") != NULL) ||               // ASCII product
        (strstr(content, "int_") != NULL && !strstr(content, "\\int")) ||  // ASCII integration
        (strstr(content, "lim_(") != NULL) ||                // ASCII limit
        // ASCII math specific operators
        (strstr(content, "!=") != NULL) ||                   // Not equal ASCII style
        (strstr(content, "**") != NULL) ||                   // Double star power
        (strstr(content, "<=") != NULL) ||                   // Less than or equal
        (strstr(content, ">=") != NULL) ||                   // Greater than or equal
        (strstr(content, "~=") != NULL) ||                   // Approximately equal
        // ASCII infinity symbol
        (strstr(content, "infinity") != NULL) ||
        (strstr(content, " oo ") != NULL) ||                 // Standalone oo as infinity
        // Greek letters in ASCII math must be standalone words, not prefixed by \
        // Only detect if explicitly NOT using LaTeX style
        (strstr(content, " alpha ") && !strstr(content, "\\")) ||
        (strstr(content, " beta ") && !strstr(content, "\\")) ||
        (strstr(content, " gamma ") && !strstr(content, "\\")) ||
        (strstr(content, " pi ") && !strstr(content, "\\"))
    );

    // Look for LaTeX-specific commands
    bool has_latex_indicators = (
        strstr(content, "\\frac") || strstr(content, "\\sum") ||
        strstr(content, "\\int") || strstr(content, "\\iint") ||
        strstr(content, "\\iiint") || strstr(content, "\\alpha") ||
        strstr(content, "\\beta") || strstr(content, "\\gamma") ||
        strstr(content, "\\pi") || strstr(content, "\\sqrt") ||
        strstr(content, "\\begin") || strstr(content, "\\end") ||
        strstr(content, "\\hookleftarrow") || strstr(content, "\\hookrightarrow") ||
        strstr(content, "\\twoheadleftarrow") || strstr(content, "\\twoheadrightarrow") ||
        strstr(content, "\\left") || strstr(content, "\\right")
    );

    // Look for Typst-specific syntax
    bool has_typst_indicators = (
        strstr(content, "frac(") || strstr(content, "sum_") ||
        strstr(content, "integral") || strstr(content, "sqrt(")
    );

    // Priority: LaTeX > ASCII > Typst > Default LaTeX
    if (has_latex_indicators) {
        return "latex";
    } else if (has_ascii_indicators) {
        return "ascii";
    } else if (has_typst_indicators) {
        return "typst";
    }

    // Default to LaTeX
    return "latex";
}

// Phase 2: Utility functions for enhanced parsing

// Detect the type of a block element - Enhanced for RST format
static BlockType detect_block_type(MarkupParser* parser, const char* line) {
    if (!line || !*line || !parser) return BLOCK_PARAGRAPH;

    const char* pos = line;
    skip_whitespace(&pos);

    // RST-specific blocks - only detected when format is RST
    if (parser->config.format == MARKUP_RST) {
        if (is_rst_transition_line(line)) {
            return BLOCK_DIVIDER;
        }

        if (is_rst_comment_line(line)) {
            return BLOCK_COMMENT;
        }

        if (is_rst_literal_block_marker(line) || line_ends_with_double_colon(line)) {
            return BLOCK_CODE_BLOCK;
        }

        if (is_rst_grid_table_line(line)) {
            return BLOCK_TABLE;
        }

        if (is_rst_definition_list_item(line) &&
            parser->current_line + 1 < parser->line_count &&
            is_rst_definition_list_definition(parser->lines[parser->current_line + 1])) {
            return BLOCK_LIST_ITEM;
        }
    }

    // Textile-specific blocks - only detected when format is TEXTILE
    if (parser->config.format == MARKUP_TEXTILE) {
        if (is_textile_comment(line)) {
            return BLOCK_COMMENT;
        }

        if (is_textile_block_code(line)) {
            return BLOCK_CODE_BLOCK;
        }

        if (is_textile_block_quote(line)) {
            return BLOCK_QUOTE;
        }

        if (is_textile_pre(line)) {
            return BLOCK_CODE_BLOCK;
        }

        if (is_textile_notextile(line)) {
            return BLOCK_COMMENT; // Treat notextile as comment-like
        }

        char list_type = 0;
        if (is_textile_list_item(line, &list_type)) {
            return BLOCK_LIST_ITEM;
        }
    }

    // AsciiDoc-specific blocks - only detected when format is ASCIIDOC
    if (parser->config.format == MARKUP_ASCIIDOC) {
        if (is_asciidoc_listing_block(line)) {
            return BLOCK_CODE_BLOCK;
        }

        if (is_asciidoc_admonition(line)) {
            return BLOCK_QUOTE; // Treat admonitions as specialized quotes
        }

        if (is_asciidoc_table_start(line)) {
            return BLOCK_TABLE;
        }

        if (is_asciidoc_list_item(line)) {
            return BLOCK_LIST_ITEM;
        }

        int level;
        if (is_asciidoc_heading(line, &level)) {
            return BLOCK_HEADER;
        }
    }

    // MediaWiki-specific blocks - only detected when format is WIKI
    if (parser->config.format == MARKUP_WIKI) {
        if (is_wiki_horizontal_rule(line)) {
            return BLOCK_DIVIDER;
        }

        if (is_wiki_table_start(line)) {
            return BLOCK_TABLE;
        }

        char marker;
        int level;
        if (is_wiki_list_item(line, &marker, &level)) {
            return BLOCK_LIST_ITEM;
        }
    }

    // CommonMark: Indented code block detection (4+ leading spaces)
    // Must be checked BEFORE header detection
    {
        int space_count = 0;
        const char* p = line;
        while (*p == ' ') {
            space_count++;
            p++;
        }
        // 4 or more spaces and non-empty content = indented code block
        if (space_count >= 4 && *p && *p != '\n' && *p != '\r') {
            return BLOCK_CODE_BLOCK;
        }
    }

    // Header detection (# ## ### etc. and RST underlined headers)
    if (get_header_level(parser, line) > 0) {
        return BLOCK_HEADER;
    }

    // List item detection (-, *, +, 1., 2., etc.)
    if (is_list_item(pos)) {
        return BLOCK_LIST_ITEM;
    }

    // Code fence detection (```, ~~~)
    if (is_code_fence(pos)) {
        return BLOCK_CODE_BLOCK;
    }

    // Blockquote detection (>)
    if (is_blockquote(pos)) {
        return BLOCK_QUOTE;
    }

    // Table row detection (|)
    if (is_table_row(pos)) {
        return BLOCK_TABLE;
    }

    // Horizontal rule detection (---, ***, ___)
    if ((*pos == '-' || *pos == '*' || *pos == '_')) {
        int count = 0;
        char marker = *pos;
        while (*pos == marker || *pos == ' ') {
            if (*pos == marker) count++;
            pos++;
        }
        if (count >= 3 && *pos == '\0') {
            return BLOCK_DIVIDER;
        }
    }

    // Math block detection ($$)
    if (*pos == '$' && *(pos+1) == '$') {
        return BLOCK_MATH;
    }

    // CommonMark HTML block detection (types 1-7)
    if (is_html_block_start(line) > 0) {
        return BLOCK_HTML;
    }

    return BLOCK_PARAGRAPH;
}

// Get header level (1-6) - Enhanced for RST underlined headers
static int get_header_level(MarkupParser* parser, const char* line) {
    if (!line || !parser) return 0;

    const char* pos = line;
    skip_whitespace(&pos);

    // Handle Markdown-style headers for all formats
    int level = 0;
    while (*pos == '#' && level < 6) {
        level++;
        pos++;
    }

    // Must be followed by space or end of line
    if (level > 0 && (*pos == ' ' || *pos == '\t' || *pos == '\0')) {
        return level;
    }

    // CommonMark setext headings: text followed by === (h1) or --- (h2)
    if (parser->config.format == MARKUP_MARKDOWN) {
        // Check indentation - no more than 3 spaces
        int spaces = 0;
        const char* p = line;
        while (*p == ' ') {
            spaces++;
            p++;
        }
        // 4+ spaces means it can't start a setext heading
        if (spaces >= 4) return 0;

        // Line must have non-empty content
        if (*p == '\0' || *p == '\n' || *p == '\r') return 0;

        // Setext heading cannot start with > (that's a blockquote)
        // or be a list item marker or thematic break pattern
        if (*p == '>') return 0;
        if (*p == '-' || *p == '*' || *p == '+') {
            // Check if this looks like a list item or thematic break, not text
            const char* check = p;
            while (*check == *p || *check == ' ') check++;
            if (*check == '\0' || *check == '\n' || *check == '\r') {
                // Looks like a thematic break (--- or ***)
                return 0;
            }
            // Check for list item (- text)
            if (p[1] == ' ' || p[1] == '\t') {
                return 0;  // List item, not setext content
            }
        }

        // Check if next line is a setext underline
        if (parser->current_line + 1 < parser->line_count) {
            const char* next_line = parser->lines[parser->current_line + 1];
            const char* next_pos = next_line;

            // Skip up to 3 spaces
            int next_spaces = 0;
            while (*next_pos == ' ' && next_spaces < 3) {
                next_spaces++;
                next_pos++;
            }
            // 4+ spaces means not an underline
            if (*next_pos == ' ') return 0;

            // Check for = or - underline
            char underline_char = *next_pos;
            if (underline_char == '=' || underline_char == '-') {
                int underline_count = 0;
                while (*next_pos == underline_char) {
                    underline_count++;
                    next_pos++;
                }
                // Skip trailing spaces and tabs
                while (*next_pos == ' ' || *next_pos == '\t') {
                    next_pos++;
                }
                // Must end with newline or end of string (at least 1 underline char)
                if (underline_count >= 1 && (*next_pos == '\0' || *next_pos == '\n' || *next_pos == '\r')) {
                    return (underline_char == '=') ? 1 : 2;
                }
            }
        }
    }

    // Handle RST-style underlined headers (only for RST format)
    if (parser->config.format == MARKUP_RST) {
        // Check if current line is text and next line is underline
        if (parser->current_line + 1 < parser->line_count) {
            const char* next_line = parser->lines[parser->current_line + 1];
            const char* next_pos = next_line;
            skip_whitespace(&next_pos);

            // Count underline characters
            char underline_char = *next_pos;
            if (underline_char && (underline_char == '=' || underline_char == '-' ||
                                 underline_char == '~' || underline_char == '^' ||
                                 underline_char == '+' || underline_char == '*')) {
                int underline_count = 0;
                while (*next_pos == underline_char) {
                    underline_count++;
                    next_pos++;
                }

                // Must be at least as long as the header text (rough check)
                size_t text_len = strlen(line);
                if (underline_count >= (int)(text_len * 0.7)) {
                    // Map underline characters to header levels (RST convention)
                    switch (underline_char) {
                        case '=': return 1;  // Main title
                        case '-': return 2;  // Subtitle
                        case '~': return 3;  // Section
                        case '^': return 4;  // Subsection
                        case '+': return 5;  // Sub-subsection
                        case '*': return 6;  // Paragraph
                        default: return 2;   // Default to level 2
                    }
                }
            }
        }
    }

    // Handle Textile-style headers (h1. to h6.) - only for TEXTILE format
    if (parser->config.format == MARKUP_TEXTILE) {
        int textile_level = 0;
        if (is_textile_heading(line, &textile_level)) {
            return textile_level;
        }
    }

    // Handle MediaWiki-style headers (= Header =) - only for WIKI format
    if (parser->config.format == MARKUP_WIKI) {
        int wiki_level = 0;
        if (is_wiki_heading(line, &wiki_level)) {
            return wiki_level;
        }
    }

    // Handle AsciiDoc-style headers (= Header, == Header ==) - only for ASCIIDOC format
    if (parser->config.format == MARKUP_ASCIIDOC) {
        int asciidoc_level = 0;
        if (is_asciidoc_heading(line, &asciidoc_level)) {
            return asciidoc_level;
        }
    }

    return 0;
}

// Check if line is a list item
static bool is_list_item(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Unordered list markers
    if (*pos == '-' || *pos == '*' || *pos == '+') {
        pos++;
        return (*pos == ' ' || *pos == '\t' || *pos == '\0');
    }

    // Ordered list markers (1., 2., etc.)
    if (isdigit(*pos)) {
        while (isdigit(*pos)) pos++;
        if (*pos == '.') {
            pos++;
            return (*pos == ' ' || *pos == '\t' || *pos == '\0');
        }
    }

    return false;
}

// Check if line is a code fence
static bool is_code_fence(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for backtick fences (```)
    if (*pos == '`') {
        int count = 0;
        while (*pos == '`') {
            count++;
            pos++;
        }
        return count >= 3;
    }

    // Check for tilde fences (~~~)
    if (*pos == '~') {
        int count = 0;
        while (*pos == '~') {
            count++;
            pos++;
        }
        return count >= 3;
    }

    return false;
}

// Check if line is blockquote
static bool is_blockquote(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    return (*pos == '>');
}

// Check if line is table row
static bool is_table_row(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Must start with | to be a table row
    if (*pos == '|') return true;

    // Don't treat lines with math expressions as tables
    // Math expressions can contain | characters (absolute value, etc.)
    if (strstr(line, "$") != NULL) {
        return false;
    }

    // Look for multiple pipe characters (table delimiter pattern)
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

// Case-insensitive prefix match helper
static bool ci_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix)) {
            return false;
        }
        str++;
        prefix++;
    }
    return true;
}

// Check if character follows valid HTML block tag (space, tab, >, />, or end of line)
static bool is_tag_end_char(char c) {
    return c == ' ' || c == '\t' || c == '>' || c == '\0' || c == '\n' || c == '\r';
}

// Check if line starts an HTML block - returns type 1-7, or 0 if not
static int is_html_block_start(const char* line) {
    if (!line) return 0;

    const char* pos = line;
    // Skip up to 3 spaces of indentation
    int spaces = 0;
    while (*pos == ' ' && spaces < 3) {
        pos++;
        spaces++;
    }
    if (*pos == ' ') return 0;  // 4+ spaces is not HTML block

    if (*pos != '<') return 0;
    pos++;

    // Type 1: <pre, <script, <style, <textarea
    if (ci_starts_with(pos, "pre") && (is_tag_end_char(pos[3]) || pos[3] == '/')) return 1;
    if (ci_starts_with(pos, "script") && (is_tag_end_char(pos[6]) || pos[6] == '/')) return 1;
    if (ci_starts_with(pos, "style") && (is_tag_end_char(pos[5]) || pos[5] == '/')) return 1;
    if (ci_starts_with(pos, "textarea") && (is_tag_end_char(pos[8]) || pos[8] == '/')) return 1;

    // Type 2: <!--
    if (pos[0] == '!' && pos[1] == '-' && pos[2] == '-') return 2;

    // Type 3: <?
    if (pos[0] == '?') return 3;

    // Type 4: <! followed by uppercase letter
    if (pos[0] == '!' && pos[1] >= 'A' && pos[1] <= 'Z') return 4;

    // Type 5: <![CDATA[
    if (ci_starts_with(pos, "![CDATA[")) return 5;

    // Type 6: Block-level HTML elements (open or close tag)
    bool is_close = false;
    if (*pos == '/') {
        is_close = true;
        pos++;
    }

    // List of block-level elements for type 6
    static const char* block_tags[] = {
        "address", "article", "aside", "base", "basefont", "blockquote", "body",
        "caption", "center", "col", "colgroup", "dd", "details", "dialog",
        "dir", "div", "dl", "dt", "fieldset", "figcaption", "figure",
        "footer", "form", "frame", "frameset",
        "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
        "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem",
        "nav", "noframes", "ol", "optgroup", "option", "p", "param",
        "search", "section", "summary", "table", "tbody", "td",
        "tfoot", "th", "thead", "title", "tr", "track", "ul", NULL
    };

    for (int i = 0; block_tags[i]; i++) {
        size_t len = strlen(block_tags[i]);
        if (ci_starts_with(pos, block_tags[i])) {
            char next = pos[len];
            if (is_tag_end_char(next) || next == '/') {
                return 6;
            }
        }
    }

    // Type 7: Complete open or close tag on single line (any tag except script/pre/style/textarea)
    // This is more complex - need to verify complete tag syntax
    // For now, detect simple complete tags like <div>, </span>, <hr/>, etc.
    if (!is_close) {
        // Check for open tag: <tagname ...> or <tagname .../>
        const char* tag_start = pos;
        // Tag name must start with ASCII letter
        if (!((*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z'))) {
            return 0;
        }
        // Skip tag name (letters, digits, hyphen)
        while ((*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z') ||
               (*pos >= '0' && *pos <= '9') || *pos == '-') {
            pos++;
        }

        // After tag name, must have whitespace, >, or />
        // If there's something else (like : for http:), it's not a valid tag
        if (*pos != ' ' && *pos != '\t' && *pos != '\n' && *pos != '\r' &&
            *pos != '>' && !(*pos == '/' && pos[1] == '>')) {
            return 0;
        }

        // Exclude type 1 tags from type 7
        size_t tag_len = pos - tag_start;
        char tag_name[32];
        if (tag_len < sizeof(tag_name)) {
            memcpy(tag_name, tag_start, tag_len);
            tag_name[tag_len] = '\0';
            if (strcasecmp(tag_name, "pre") == 0 || strcasecmp(tag_name, "script") == 0 ||
                strcasecmp(tag_name, "style") == 0 || strcasecmp(tag_name, "textarea") == 0) {
                return 0;  // These are type 1, not type 7
            }
        }

        // Skip attributes (simplified: anything until > or />)
        while (*pos && *pos != '>' && !(*pos == '/' && pos[1] == '>')) {
            pos++;
        }
        if (*pos == '/' && pos[1] == '>') {
            pos += 2;
        } else if (*pos == '>') {
            pos++;
        } else {
            return 0;  // Incomplete tag
        }
        // After tag, must be only whitespace until end of line
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos == '\0' || *pos == '\n' || *pos == '\r') {
            return 7;
        }
    } else {
        // Close tag: </tagname>
        const char* tag_start = pos;
        if (!((*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z'))) {
            return 0;
        }
        while ((*pos >= 'a' && *pos <= 'z') || (*pos >= 'A' && *pos <= 'Z') ||
               (*pos >= '0' && *pos <= '9') || *pos == '-') {
            pos++;
        }
        // Skip whitespace before >
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos != '>') return 0;
        pos++;
        // After tag, must be only whitespace until end of line
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos == '\0' || *pos == '\n' || *pos == '\r') {
            return 7;
        }
    }

    return 0;
}

// Case-insensitive substring search
static const char* ci_strstr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n) return haystack;
    }
    return NULL;
}

// Check if line ends an HTML block of given type
static bool is_html_block_end(const char* line, int html_block_type) {
    if (!line) return false;

    switch (html_block_type) {
        case 1:
            // End on </pre>, </script>, </style>, </textarea>
            return ci_strstr(line, "</pre>") || ci_strstr(line, "</script>") ||
                   ci_strstr(line, "</style>") || ci_strstr(line, "</textarea>");
        case 2:
            // End on -->
            return strstr(line, "-->") != NULL;
        case 3:
            // End on ?>
            return strstr(line, "?>") != NULL;
        case 4:
            // End on >
            return strchr(line, '>') != NULL;
        case 5:
            // End on ]]>
            return strstr(line, "]]>") != NULL;
        case 6:
        case 7:
            // End on blank line (checked in parse_html_block)
            return false;
        default:
            return false;
    }
}

// Parse HTML block - raw HTML passthrough
static Item parse_html_block(MarkupParser* parser, const char* line) {
    int html_type = is_html_block_start(line);
    if (html_type == 0) {
        return parse_paragraph(parser, line);
    }

    // Create html-block element to hold raw content
    Element* html_elem = create_element(parser, "html-block");
    if (!html_elem) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Collect all lines until end condition
    StringBuf* sb = stringbuf_new(parser->input()->pool);

    bool first_line = true;

    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // For types 6 and 7, end on blank line
        if ((html_type == 6 || html_type == 7) && !first_line) {
            if (is_empty_line(current)) {
                break;
            }
        }

        // Add line to content
        if (!first_line) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, current);

        // Check for type-specific end condition
        if (html_type >= 1 && html_type <= 5) {
            if (is_html_block_end(current, html_type)) {
                parser->current_line++;
                break;
            }
        }

        parser->current_line++;
        first_line = false;
    }

    // Store the raw HTML content
    if (sb->length > 0) {
        String* content_str = stringbuf_to_string(sb);
        if (content_str) {
            list_push((List*)html_elem, (Item){.item = s2it(content_str)});
            increment_element_content_length(html_elem);
        }
    }

    // Add type attribute for debugging
    char type_str[8];
    snprintf(type_str, sizeof(type_str), "%d", html_type);
    add_attribute_to_element(parser, html_elem, "type", type_str);

    return (Item){.item = (uint64_t)html_elem};
}

// Phase 4: Advanced inline element parsers

// Parse strikethrough text (~~text~~)
static Item parse_strikethrough(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening ~~
    if (*start != '~' || *(start+1) != '~') {
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = start + 2;
    const char* content_start = pos;

    // Find closing ~~
    while (*pos && !(*pos == '~' && *(pos+1) == '~')) {
        pos++;
    }

    if (!*pos || *(pos+1) != '~') {
        // No closing ~~, not strikethrough
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content between ~~
    size_t content_len = pos - content_start;
    if (content_len == 0) {
        *text = pos + 2; // Skip closing ~~
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Create strikethrough element
    Element* s_elem = create_element(parser, "s");
    if (!s_elem) {
        return (Item){.item = ITEM_ERROR};
    }

    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    // Add content as simple string (avoid recursive parsing for now to prevent crashes)
    String* content_str = create_string(parser, content);
    if (content_str) {
        Item content_item = {.item = s2it(content_str)};
        list_push((List*)s_elem, content_item);
        increment_element_content_length(s_elem);
    }

    free(content);
    *text = pos + 2; // Skip closing ~~

    return (Item){.item = (uint64_t)s_elem};
}

// Parse superscript (^text^)
static Item parse_superscript(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening ^
    if (*start != '^') {
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing ^ (but not at the beginning)
    while (*pos && *pos != '^' && !isspace(*pos)) {
        pos++;
    }

    if (*pos != '^' || pos == content_start) {
        // No proper closing ^ or empty content
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content between ^
    size_t content_len = pos - content_start;

    // Create superscript element
    Element* sup_elem = create_element(parser, "sup");
    if (!sup_elem) {
        return (Item){.item = ITEM_ERROR};
    }

    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    // Add content as string (superscripts are usually simple)
    String* content_str = create_string(parser, content);
    if (content_str) {
        Item text_item = {.item = s2it(content_str)};
        list_push((List*)sup_elem, text_item);
        increment_element_content_length(sup_elem);
    }

    free(content);
    *text = pos + 1; // Skip closing ^

    return (Item){.item = (uint64_t)sup_elem};
}

// Parse subscript (~text~)
static Item parse_subscript(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening ~
    if (*start != '~') {
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing ~ (but not at the beginning)
    while (*pos && *pos != '~' && !isspace(*pos)) {
        pos++;
    }

    if (*pos != '~' || pos == content_start) {
        // No proper closing ~ or empty content
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content between ~
    size_t content_len = pos - content_start;

    // Create subscript element
    Element* sub_elem = create_element(parser, "sub");
    if (!sub_elem) {
        return (Item){.item = ITEM_ERROR};
    }

    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    // Add content as string (subscripts are usually simple)
    String* content_str = create_string(parser, content);
    if (content_str) {
        Item text_item = {.item = s2it(content_str)};
        list_push((List*)sub_elem, text_item);
        increment_element_content_length(sub_elem);
    }

    free(content);
    *text = pos + 1; // Skip closing ~

    return (Item){.item = (uint64_t)sub_elem};
}

// Comprehensive GitHub Emoji shortcode mapping table
static const struct {
    const char* shortcode;
    const char* emoji;
} emoji_map[] = {
    // Smileys & Emotion
    {":smile:", "😄"},
    {":smiley:", "😃"},
    {":grinning:", "😀"},
    {":blush:", "😊"},
    {":relaxed:", "☺️"},
    {":wink:", "😉"},
    {":heart_eyes:", "😍"},
    {":kissing_heart:", "😘"},
    {":kissing_closed_eyes:", "😚"},
    {":stuck_out_tongue:", "😛"},
    {":stuck_out_tongue_winking_eye:", "�"},
    {":stuck_out_tongue_closed_eyes:", "😝"},
    {":disappointed:", "😞"},
    {":worried:", "�"},
    {":angry:", "😠"},
    {":rage:", "😡"},
    {":cry:", "😢"},
    {":persevere:", "😣"},
    {":triumph:", "😤"},
    {":disappointed_relieved:", "😥"},
    {":frowning:", "😦"},
    {":anguished:", "😧"},
    {":fearful:", "😨"},
    {":weary:", "😩"},
    {":sleepy:", "😪"},
    {":tired_face:", "😫"},
    {":grimacing:", "�"},
    {":sob:", "😭"},
    {":open_mouth:", "😮"},
    {":hushed:", "😯"},
    {":cold_sweat:", "😰"},
    {":scream:", "😱"},
    {":astonished:", "😲"},
    {":flushed:", "�"},
    {":sleeping:", "😴"},
    {":dizzy_face:", "😵"},
    {":no_mouth:", "😶"},
    {":mask:", "😷"},
    {":sunglasses:", "😎"},
    {":confused:", "😕"},
    {":neutral_face:", "😐"},
    {":expressionless:", "😑"},
    {":unamused:", "😒"},
    {":sweat_smile:", "😅"},
    {":sweat:", "😓"},
    {":joy:", "😂"},
    {":laughing:", "😆"},
    {":innocent:", "😇"},
    {":smiling_imp:", "😈"},
    {":imp:", "�"},
    {":skull:", "💀"},

    // People & Body
    {":wave:", "👋"},
    {":raised_hand:", "✋"},
    {":open_hands:", "👐"},
    {":point_up:", "☝️"},
    {":point_down:", "👇"},
    {":point_left:", "👈"},
    {":point_right:", "👉"},
    {":raised_hands:", "🙌"},
    {":pray:", "🙏"},
    {":clap:", "👏"},
    {":muscle:", "💪"},
    {":walking:", "🚶"},
    {":runner:", "🏃"},
    {":dancer:", "💃"},
    {":ok_hand:", "👌"},
    {":thumbsup:", "�"},
    {":thumbsdown:", "👎"},
    {":punch:", "👊"},
    {":fist:", "✊"},
    {":v:", "✌️"},
    {":hand:", "✋"},

    // Animals & Nature
    {":dog:", "🐶"},
    {":cat:", "🐱"},
    {":mouse:", "🐭"},
    {":hamster:", "🐹"},
    {":rabbit:", "🐰"},
    {":bear:", "🐻"},
    {":panda_face:", "�"},
    {":koala:", "🐨"},
    {":tiger:", "🐯"},
    {":lion_face:", "🦁"},
    {":cow:", "🐮"},
    {":pig:", "🐷"},
    {":pig_nose:", "🐽"},
    {":frog:", "🐸"},
    {":octopus:", "🐙"},
    {":monkey_face:", "🐵"},
    {":see_no_evil:", "🙈"},
    {":hear_no_evil:", "🙉"},
    {":speak_no_evil:", "🙊"},
    {":monkey:", "🐒"},
    {":chicken:", "🐔"},
    {":penguin:", "🐧"},
    {":bird:", "🐦"},
    {":baby_chick:", "🐤"},
    {":hatched_chick:", "🐣"},
    {":hatching_chick:", "🐣"},
    {":wolf:", "🐺"},
    {":boar:", "🐗"},
    {":horse:", "🐴"},
    {":unicorn:", "🦄"},
    {":bee:", "🐝"},
    {":bug:", "🐛"},
    {":snail:", "🐌"},
    {":beetle:", "🐞"},
    {":ant:", "🐜"},
    {":spider:", "🕷️"},
    {":scorpion:", "🦂"},
    {":crab:", "🦀"},
    {":snake:", "🐍"},
    {":turtle:", "🐢"},
    {":tropical_fish:", "🐠"},
    {":fish:", "🐟"},
    {":blowfish:", "🐡"},
    {":dolphin:", "🐬"},
    {":whale:", "🐳"},
    {":whale2:", "🐋"},
    {":crocodile:", "🐊"},
    {":leopard:", "🐆"},
    {":tiger2:", "🐅"},
    {":water_buffalo:", "🐃"},
    {":ox:", "🐂"},
    {":cow2:", "🐄"},
    {":dromedary_camel:", "🐪"},
    {":camel:", "🐫"},
    {":elephant:", "🐘"},
    {":goat:", "🐐"},
    {":ram:", "🐏"},
    {":sheep:", "🐑"},
    {":racehorse:", "🐎"},
    {":pig2:", "🐖"},
    {":rat:", "�"},
    {":mouse2:", "🐁"},
    {":rooster:", "🐓"},
    {":turkey:", "🦃"},
    {":dove:", "🕊️"},
    {":dog2:", "🐕"},
    {":poodle:", "🐩"},
    {":cat2:", "🐈"},
    {":rabbit2:", "�"},
    {":chipmunk:", "🐿️"},
    {":feet:", "🐾"},
    {":dragon:", "🐉"},
    {":dragon_face:", "🐲"},

    // Food & Drink
    {":green_apple:", "🍏"},
    {":apple:", "🍎"},
    {":pear:", "🍐"},
    {":tangerine:", "🍊"},
    {":lemon:", "🍋"},
    {":banana:", "🍌"},
    {":watermelon:", "🍉"},
    {":grapes:", "🍇"},
    {":strawberry:", "🍓"},
    {":melon:", "🍈"},
    {":cherries:", "🍒"},
    {":peach:", "🍑"},
    {":pineapple:", "🍍"},
    {":tomato:", "🍅"},
    {":eggplant:", "🍆"},
    {":hot_pepper:", "🌶️"},
    {":corn:", "🌽"},
    {":sweet_potato:", "🍠"},
    {":honey_pot:", "🍯"},
    {":bread:", "🍞"},
    {":cheese:", "🧀"},
    {":poultry_leg:", "🍗"},
    {":meat_on_bone:", "🍖"},
    {":fried_shrimp:", "🍤"},
    {":egg:", "🥚"},
    {":hamburger:", "🍔"},
    {":fries:", "🍟"},
    {":hotdog:", "🌭"},
    {":pizza:", "🍕"},
    {":spaghetti:", "🍝"},
    {":taco:", "🌮"},
    {":burrito:", "🌯"},
    {":ramen:", "🍜"},
    {":stew:", "🍲"},
    {":fish_cake:", "🍥"},
    {":sushi:", "🍣"},
    {":bento:", "🍱"},
    {":curry:", "🍛"},
    {":rice_ball:", "🍙"},
    {":rice:", "🍚"},
    {":rice_cracker:", "🍘"},
    {":oden:", "🍢"},
    {":dango:", "🍡"},
    {":shaved_ice:", "🍧"},
    {":ice_cream:", "🍨"},
    {":icecream:", "🍦"},
    {":cake:", "🍰"},
    {":birthday:", "🎂"},
    {":custard:", "🍮"},
    {":candy:", "🍬"},
    {":lollipop:", "🍭"},
    {":chocolate_bar:", "🍫"},
    {":popcorn:", "🍿"},
    {":doughnut:", "🍩"},
    {":cookie:", "🍪"},
    {":beer:", "🍺"},
    {":beers:", "🍻"},
    {":wine_glass:", "🍷"},
    {":cocktail:", "🍸"},
    {":tropical_drink:", "🍹"},
    {":champagne:", "🍾"},
    {":sake:", "🍶"},
    {":tea:", "🍵"},
    {":coffee:", "☕"},
    {":baby_bottle:", "🍼"},
    {":milk:", "🥛"},

    // Activities & Sports
    {":soccer:", "⚽"},
    {":basketball:", "🏀"},
    {":football:", "🏈"},
    {":baseball:", "⚾"},
    {":tennis:", "🎾"},
    {":volleyball:", "🏐"},
    {":rugby_football:", "🏉"},
    {":8ball:", "🎱"},
    {":golf:", "⛳"},
    {":golfer:", "🏌️"},
    {":ping_pong:", "🏓"},
    {":badminton:", "🏸"},
    {":hockey:", "🏒"},
    {":field_hockey:", "🏑"},
    {":cricket:", "🏏"},
    {":ski:", "🎿"},
    {":skier:", "⛷️"},
    {":snowboarder:", "🏂"},
    {":ice_skate:", "⛸️"},
    {":bow_and_arrow:", "🏹"},
    {":fishing_pole_and_fish:", "🎣"},
    {":rowboat:", "🚣"},
    {":swimmer:", "🏊"},
    {":surfer:", "🏄"},
    {":bath:", "🛀"},
    {":basketball_player:", "⛹️"},
    {":lifter:", "🏋️"},
    {":bicyclist:", "🚴"},
    {":mountain_bicyclist:", "🚵"},
    {":horse_racing:", "🏇"},
    {":trophy:", "🏆"},
    {":running_shirt_with_sash:", "🎽"},
    {":medal:", "🏅"},

    // Travel & Places
    {":red_car:", "🚗"},
    {":taxi:", "🚕"},
    {":blue_car:", "🚙"},
    {":bus:", "🚌"},
    {":trolleybus:", "🚎"},
    {":race_car:", "🏎️"},
    {":police_car:", "🚓"},
    {":ambulance:", "🚑"},
    {":fire_engine:", "🚒"},
    {":minibus:", "🚐"},
    {":truck:", "🚚"},
    {":articulated_lorry:", "🚛"},
    {":tractor:", "🚜"},
    {":motorcycle:", "🏍️"},
    {":bike:", "🚲"},
    {":helicopter:", "🚁"},
    {":airplane:", "✈️"},
    {":rocket:", "🚀"},
    {":satellite:", "�"},
    {":anchor:", "⚓"},
    {":ship:", "🚢"},

    // Objects
    {":watch:", "⌚"},
    {":iphone:", "📱"},
    {":calling:", "📲"},
    {":computer:", "�"},
    {":keyboard:", "⌨️"},
    {":desktop:", "🖥️"},
    {":printer:", "�️"},
    {":camera:", "📷"},
    {":camera_with_flash:", "📸"},
    {":video_camera:", "📹"},
    {":movie_camera:", "🎥"},
    {":tv:", "📺"},
    {":radio:", "📻"},
    {":microphone2:", "🎙️"},
    {":stopwatch:", "⏱️"},
    {":timer:", "⏲️"},
    {":alarm_clock:", "⏰"},
    {":clock:", "🕰️"},
    {":hourglass_flowing_sand:", "⏳"},
    {":hourglass:", "⌛"},
    {":battery:", "�"},
    {":electric_plug:", "🔌"},
    {":bulb:", "💡"},
    {":flashlight:", "�"},
    {":candle:", "🕯️"},
    {":moneybag:", "💰"},
    {":credit_card:", "💳"},
    {":gem:", "💎"},
    {":scales:", "⚖️"},
    {":wrench:", "🔧"},
    {":hammer:", "🔨"},
    {":tools:", "🛠️"},
    {":pick:", "⛏️"},
    {":nut_and_bolt:", "🔩"},
    {":gear:", "⚙️"},
    {":gun:", "🔫"},
    {":bomb:", "💣"},
    {":knife:", "🔪"},
    {":crystal_ball:", "🔮"},
    {":telescope:", "🔭"},
    {":microscope:", "🔬"},
    {":pill:", "💊"},
    {":syringe:", "💉"},
    {":thermometer:", "🌡️"},
    {":toilet:", "🚽"},
    {":shower:", "�"},
    {":bathtub:", "🛁"},

    // Symbols
    {":heart:", "❤️"},
    {":orange_heart:", "🧡"},
    {":yellow_heart:", "💛"},
    {":green_heart:", "💚"},
    {":blue_heart:", "�"},
    {":purple_heart:", "💜"},
    {":brown_heart:", "🤎"},
    {":black_heart:", "�"},
    {":white_heart:", "🤍"},
    {":broken_heart:", "�"},
    {":heart_exclamation:", "❣️"},
    {":two_hearts:", "💕"},
    {":revolving_hearts:", "💞"},
    {":heartbeat:", "💓"},
    {":heartpulse:", "💗"},
    {":sparkling_heart:", "💖"},
    {":cupid:", "💘"},
    {":gift_heart:", "💝"},
    {":heart_decoration:", "�"},
    {":peace:", "☮️"},
    {":cross:", "✝️"},
    {":star_and_crescent:", "☪️"},
    {":om_symbol:", "🕉️"},
    {":wheel_of_dharma:", "☸️"},
    {":star_of_david:", "✡️"},
    {":six_pointed_star:", "🔯"},
    {":menorah:", "🕎"},
    {":yin_yang:", "☯️"},
    {":orthodox_cross:", "☦️"},
    {":place_of_worship:", "🛐"},
    {":aries:", "♈"},
    {":taurus:", "♉"},
    {":gemini:", "♊"},
    {":cancer:", "♋"},
    {":leo:", "♌"},
    {":virgo:", "♍"},
    {":libra:", "♎"},
    {":scorpius:", "♏"},
    {":sagittarius:", "♐"},
    {":capricorn:", "♑"},
    {":aquarius:", "♒"},
    {":pisces:", "♓"},
    {":id:", "🆔"},
    {":atom:", "⚛️"},
    {":accept:", "🉑"},
    {":radioactive:", "☢️"},
    {":biohazard:", "☣️"},
    {":mobile_phone_off:", "�"},
    {":vibration_mode:", "📳"},
    {":eight_pointed_black_star:", "✴️"},
    {":vs:", "🆚"},
    {":white_flower:", "💮"},
    {":secret:", "㊙️"},
    {":congratulations:", "㊗️"},
    {":a:", "🅰️"},
    {":b:", "🅱️"},
    {":ab:", "🆎"},
    {":cl:", "🆑"},
    {":o2:", "🅾️"},
    {":sos:", "🆘"},
    {":x:", "❌"},
    {":o:", "⭕"},
    {":octagonal_sign:", "🛑"},
    {":no_entry:", "⛔"},
    {":name_badge:", "📛"},
    {":no_entry_sign:", "🚫"},
    {":100:", "💯"},
    {":anger:", "💢"},
    {":hotsprings:", "♨️"},
    {":no_pedestrians:", "🚷"},
    {":do_not_litter:", "�"},
    {":no_bicycles:", "🚳"},
    {":non-potable_water:", "�"},
    {":underage:", "�"},
    {":no_mobile_phones:", "📵"},
    {":no_smoking:", "🚭"},
    {":exclamation:", "❗"},
    {":grey_exclamation:", "❕"},
    {":question:", "❓"},
    {":grey_question:", "❔"},
    {":bangbang:", "‼️"},
    {":interrobang:", "⁉️"},
    {":low_brightness:", "🔅"},
    {":high_brightness:", "🔆"},
    {":warning:", "⚠️"},
    {":children_crossing:", "🚸"},
    {":trident:", "🔱"},
    {":beginner:", "🔰"},
    {":recycle:", "♻️"},
    {":white_check_mark:", "✅"},
    {":chart:", "�"},
    {":sparkle:", "❇️"},
    {":eight_spoked_asterisk:", "✳️"},
    {":negative_squared_cross_mark:", "❎"},
    {":globe_with_meridians:", "🌐"},
    {":diamond_shape_with_a_dot_inside:", "�"},
    {":m:", "Ⓜ️"},
    {":cyclone:", "🌀"},
    {":zzz:", "💤"},
    {":atm:", "🏧"},
    {":wc:", "🚾"},
    {":wheelchair:", "♿"},
    {":parking:", "🅿️"},
    {":mens:", "🚹"},
    {":womens:", "🚺"},
    {":baby_symbol:", "🚼"},
    {":restroom:", "🚻"},
    {":put_litter_in_its_place:", "🚮"},
    {":cinema:", "🎦"},
    {":signal_strength:", "📶"},
    {":symbols:", "�"},
    {":information_source:", "ℹ️"},
    {":abc:", "🔤"},
    {":abcd:", "🔡"},
    {":capital_abcd:", "🔠"},
    {":ng:", "🆖"},
    {":ok:", "🆗"},
    {":up:", "🆙"},
    {":cool:", "🆒"},
    {":new:", "🆕"},
    {":free:", "🆓"},
    {":zero:", "0️⃣"},
    {":one:", "1️⃣"},
    {":two:", "2️⃣"},
    {":three:", "3️⃣"},
    {":four:", "4️⃣"},
    {":five:", "5️⃣"},
    {":six:", "6️⃣"},
    {":seven:", "7️⃣"},
    {":eight:", "8️⃣"},
    {":nine:", "9️⃣"},
    {":keycap_ten:", "�"},
    {":hash:", "#️⃣"},
    {":asterisk:", "*️⃣"},

    // GitHub specific
    {":octocat:", "🐙"},
    {":shipit:", "🚀"},
    {":bowtie:", "👔"},

    // Programming/Tech
    {":bug:", "🐛"},
    {":key:", "🔑"},
    {":lock:", "🔒"},
    {":unlock:", "🔓"},
    {":link:", "🔗"},
    {":paperclip:", "�"},
    {":mag:", "🔍"},
    {":mag_right:", "🔎"},
    {":email:", "✉️"},
    {":phone:", "�"},
    {":book:", "📖"},
    {":pencil:", "✏️"},
    {":memo:", "📝"},
    {":mailbox:", "📮"},
    {":inbox_tray:", "📥"},

    // Nature symbols
    {":cactus:", "🌵"},
    {":christmas_tree:", "🎄"},
    {":evergreen_tree:", "🌲"},
    {":deciduous_tree:", "🌳"},
    {":palm_tree:", "🌴"},
    {":seedling:", "🌱"},
    {":herb:", "🌿"},
    {":shamrock:", "☘️"},
    {":four_leaf_clover:", "🍀"},
    {":bamboo:", "🎍"},
    {":tanabata_tree:", "🎋"},
    {":leaves:", "🍃"},
    {":fallen_leaf:", "🍂"},
    {":maple_leaf:", "🍁"},
    {":ear_of_rice:", "🌾"},
    {":hibiscus:", "🌺"},
    {":sunflower:", "🌻"},
    {":rose:", "🌹"},
    {":tulip:", "🌷"},
    {":blossom:", "🌼"},
    {":cherry_blossom:", "🌸"},
    {":bouquet:", "💐"},
    {":mushroom:", "🍄"},
    {":chestnut:", "🌰"},
    {":jack_o_lantern:", "🎃"},
    {":shell:", "🐚"},
    {":spider_web:", "�️"},
    {":earth_americas:", "🌎"},
    {":earth_africa:", "🌍"},
    {":earth_asia:", "🌏"},
    {":full_moon:", "🌕"},
    {":waning_gibbous_moon:", "🌖"},
    {":last_quarter_moon:", "🌗"},
    {":waning_crescent_moon:", "🌘"},
    {":new_moon:", "🌑"},
    {":waxing_crescent_moon:", "🌒"},
    {":first_quarter_moon:", "🌓"},
    {":moon:", "🌔"},
    {":new_moon_with_face:", "🌚"},
    {":full_moon_with_face:", "🌝"},
    {":first_quarter_moon_with_face:", "🌛"},
    {":last_quarter_moon_with_face:", "🌜"},
    {":sun_with_face:", "🌞"},
    {":crescent_moon:", "🌙"},
    {":star:", "⭐"},
    {":star2:", "🌟"},
    {":dizzy:", "💫"},
    {":sparkles:", "✨"},
    {":comet:", "☄️"},
    {":sunny:", "☀️"},
    {":partly_sunny:", "⛅"},
    {":cloud:", "☁️"},
    {":zap:", "⚡"},
    {":fire:", "�"},
    {":boom:", "💥"},
    {":snowflake:", "❄️"},
    {":snowman2:", "⛄"},
    {":snowman:", "☃️"},
    {":umbrella:", "☔"},
    {":droplet:", "💧"},
    {":sweat_drops:", "💦"},
    {":ocean:", "🌊"},

    {NULL, NULL}  // End marker
};

// Parse emoji shortcode (:emoji:)
// Returns a Symbol item with the shortcode name (without colons) for roundtrip compatibility
static Item parse_emoji_shortcode(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening :
    if (*start != ':') {
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing : (look for word characters and underscores)
    while (*pos && (isalnum(*pos) || *pos == '_')) {
        pos++;
    }

    if (*pos != ':' || pos == content_start) {
        // No proper closing : or empty content
        return (Item){.item = ITEM_ERROR};
    }

    // Extract shortcode name (without the colons)
    size_t name_len = pos - content_start;
    char* shortcode_name = (char*)malloc(name_len + 1);
    if (!shortcode_name) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(shortcode_name, content_start, name_len);
    shortcode_name[name_len] = '\0';

    // Build full shortcode with colons for lookup
    char* full_shortcode = (char*)malloc(name_len + 3);
    if (!full_shortcode) {
        free(shortcode_name);
        return (Item){.item = ITEM_ERROR};
    }
    full_shortcode[0] = ':';
    strncpy(full_shortcode + 1, shortcode_name, name_len);
    full_shortcode[name_len + 1] = ':';
    full_shortcode[name_len + 2] = '\0';

    // Look up emoji in table to validate it exists
    const char* emoji_char = NULL;
    for (int i = 0; emoji_map[i].shortcode; i++) {
        if (strcmp(full_shortcode, emoji_map[i].shortcode) == 0) {
            emoji_char = emoji_map[i].emoji;
            break;
        }
    }

    free(full_shortcode);

    if (!emoji_char) {
        // Unknown emoji shortcode - return error so it's preserved as literal text
        free(shortcode_name);
        return (Item){.item = ITEM_ERROR};
    }

    // Create Symbol item with the shortcode name (e.g., "smile" for :smile:)
    String* symbol_str = create_symbol(parser, shortcode_name);
    free(shortcode_name);

    if (!symbol_str) {
        return (Item){.item = ITEM_ERROR};
    }

    *text = pos + 1; // Skip closing :

    // Return as Symbol type using y2it (symbol to item)
    return (Item){.item = y2it(symbol_str)};
}

// Parse inline math ($expression$)
static Item parse_inline_math(MarkupParser* parser, const char** text) {
    const char* start = *text;

    // Check for opening $
    if (*start != '$') {
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = start + 1;
    const char* content_start = pos;

    // Find closing $ (but don't allow empty content)
    while (*pos && *pos != '$') {
        pos++;
    }

    if (*pos != '$' || pos == content_start) {
        // No proper closing $ or empty content
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content between $
    size_t content_len = pos - content_start;

    // Create math element
    Element* math_elem = create_element(parser, "math");
    if (!math_elem) {
        return (Item){.item = ITEM_ERROR};
    }

    // Add type attribute for inline math
    add_attribute_to_element(parser, math_elem, "type", "inline");

    // Create content string with extra padding to prevent buffer overflow
    char* content = (char*)malloc(content_len + 16);  // Add 16 bytes padding
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';
    // Zero out the padding to ensure clean memory
    memset(content + content_len + 1, 0, 15);

    // Parse the math content using the math parser
    const char* math_flavor = detect_math_flavor(content);
    InputContext math_ctx(parser->input(), content, content_len);
    Item parsed_math = parse_math_content(math_ctx, content, math_flavor);

    if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
        // Check if parsing was successful by verifying content wasn't lost
        // For now, always use parsed result if parser didn't error
        list_push((List*)math_elem, parsed_math);
        increment_element_content_length(math_elem);
    } else {
        // Fallback to plain text if math parsing fails
        // This preserves the original LaTeX content
        String* math_str = create_string(parser, content);
        if (math_str) {
            Item math_item = {.item = s2it(math_str)};
            list_push((List*)math_elem, math_item);
            increment_element_content_length(math_elem);
        }
    }

    free(content);
    *text = pos + 1; // Skip closing $

    return (Item){.item = (uint64_t)math_elem};
}

// Parse ASCII math with prefix (asciimath:: or AM::)
static Item parse_ascii_math_prefix(MarkupParser* parser, const char** text) {
    const char* start = *text;
    log_debug("*** DEBUG: ENTERING parse_ascii_math_prefix with text: '%.20s' ***\n", start);

    // Check for asciimath:: or AM:: prefix
    bool is_asciimath = false;
    bool is_am_prefix = false;
    const char* pos = start;

    if (strncmp(pos, "asciimath::", 11) == 0) {
        pos += 11;
        is_asciimath = true;
        log_debug("DEBUG: Found asciimath:: prefix\n");
    } else if (strncmp(pos, "AM::", 4) == 0) {
        pos += 4;
        is_asciimath = true;
        is_am_prefix = true;
        log_debug("DEBUG: Found AM:: prefix\n");
    }

    if (!is_asciimath) {
        log_debug("DEBUG: No ASCII math prefix found, returning error\n");
        return (Item){.item = ITEM_ERROR};
    }

    log_debug("DEBUG: ASCII math prefix detected, is_am_prefix = %s\n", is_am_prefix ? "true" : "false");

    // Find the end of the math expression (end of line or whitespace)
    const char* content_start = pos;
    log_debug("DEBUG: Content starts at: '%.20s'\n", content_start);
    while (*pos && *pos != '\n' && *pos != '\r') {
        pos++;
    }

    // Trim trailing whitespace
    while (pos > content_start && (*(pos-1) == ' ' || *(pos-1) == '\t')) {
        pos--;
    }

    if (pos == content_start) {
        // No content after prefix
        log_debug("DEBUG: No content after prefix\n");
        return (Item){.item = ITEM_ERROR};
    }

    // Extract content
    size_t content_len = pos - content_start;
    log_debug("DEBUG: Content length: %zu\n", content_len);

    // Create math element
    Element* math_elem = create_element(parser, "math");
    if (!math_elem) {
        return (Item){.item = ITEM_ERROR};
    }

    // Add type attribute for ASCII math
    add_attribute_to_element(parser, math_elem, "type", "ascii");
    // Set flavor based on the prefix used
    if (is_am_prefix) {
        log_debug("DEBUG: Setting flavor to 'AM' for AM:: prefix\n");
        add_attribute_to_element(parser, math_elem, "flavor", "AM");
    } else {
        log_debug("DEBUG: Setting flavor to 'ascii' for asciimath:: prefix\n");
        add_attribute_to_element(parser, math_elem, "flavor", "ascii");
    }

    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    log_debug("DEBUG: Extracted content: '%s'\n", content);

    // Parse the math content using ASCII flavor
    InputContext math_ctx(parser->input(), content, content_len);
    Item parsed_math = parse_math_content(math_ctx, content, "ascii");
    log_debug("DEBUG: Math parsing result: %s\n", (parsed_math.item == ITEM_ERROR) ? "ERROR" :
           (parsed_math.item == ITEM_UNDEFINED) ? "UNDEFINED" : "SUCCESS");

    if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
        list_push((List*)math_elem, parsed_math);
        increment_element_content_length(math_elem);
    } else {
        // Fallback to plain text if math parsing fails
        String* math_str = create_string(parser, content);
        if (math_str) {
            Item math_item = {.item = s2it(math_str)};
            list_push((List*)math_elem, math_item);
            increment_element_content_length(math_elem);
        }
    }

    free(content);
    *text = pos;

    return (Item){.item = (uint64_t)math_elem};
}

// Parse small caps (placeholder for future implementation)
static Item parse_small_caps(MarkupParser* parser, const char** text) {
    // Small caps could be implemented as HTML <span style="font-variant: small-caps">
    // This is a placeholder for future implementation
    return (Item){.item = ITEM_UNDEFINED};
}

// Phase 6: Advanced features implementation

// Check if line is a footnote definition ([^1]: content)
static bool is_footnote_definition(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for [^
    if (*pos != '[' || *(pos+1) != '^') return false;
    pos += 2;

    // Check for identifier
    if (!*pos || isspace(*pos)) return false;

    // Find closing ]:
    while (*pos && *pos != ']') pos++;
    if (*pos != ']' || *(pos+1) != ':') return false;

    return true;
}

// Parse footnote definition ([^1]: This is a footnote)
static Item parse_footnote_definition(MarkupParser* parser, const char* line) {
    Element* footnote = create_element(parser, "footnote");
    if (!footnote) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = line;
    skip_whitespace(&pos);
    pos += 2; // Skip [^

    // Extract footnote ID
    const char* id_start = pos;
    while (*pos && *pos != ']') pos++;

    size_t id_len = pos - id_start;
    char* id = (char*)malloc(id_len + 1);
    if (id) {
        strncpy(id, id_start, id_len);
        id[id_len] = '\0';
        add_attribute_to_element(parser, footnote, "id", id);
        free(id);
    }

    // Skip ]: and parse content
    pos += 2; // Skip ]:
    skip_whitespace(&pos);

    if (*pos) {
        Item content = parse_inline_spans(parser, pos);
        if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
            list_push((List*)footnote, content);
            increment_element_content_length(footnote);
        }
    }

    parser->current_line++;
    return (Item){.item = (uint64_t)footnote};
}

// Parse footnote reference ([^1])
static Item parse_footnote_reference(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for [^
    if (*pos != '[' || *(pos+1) != '^') {
        return (Item){.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip [^
    const char* id_start = pos;

    // Find closing ]
    while (*pos && *pos != ']') pos++;

    if (*pos != ']') {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* ref = create_element(parser, "footnote-ref");
    if (!ref) {
        *text = pos + 1;
        return (Item){.item = ITEM_ERROR};
    }

    // Extract and add ID
    size_t id_len = pos - id_start;
    char* id = (char*)malloc(id_len + 1);
    if (id) {
        strncpy(id, id_start, id_len);
        id[id_len] = '\0';
        add_attribute_to_element(parser, ref, "ref", id);
        free(id);
    }

    *text = pos + 1; // Skip closing ]
    return (Item){.item = (uint64_t)ref};
}

// Parse citations [@key] or [@key, p. 123]
static Item parse_citation(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for [@
    if (*pos != '[' || *(pos+1) != '@') {
        return (Item){.item = ITEM_UNDEFINED};
    }

    pos += 2; // Skip [@
    const char* key_start = pos;

    // Find end of citation key (space, comma, or ])
    while (*pos && *pos != ' ' && *pos != ',' && *pos != ']') pos++;

    if (pos == key_start) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* citation = create_element(parser, "citation");
    if (!citation) {
        *text = pos;
        return (Item){.item = ITEM_ERROR};
    }

    // Extract citation key
    size_t key_len = pos - key_start;
    char* key = (char*)malloc(key_len + 1);
    if (key) {
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        add_attribute_to_element(parser, citation, "key", key);
        free(key);
    }

    // Check for additional citation info (page numbers, etc.)
    if (*pos == ',' || *pos == ' ') {
        skip_whitespace(&pos);
        if (*pos == ',') {
            pos++;
            skip_whitespace(&pos);
        }

        const char* info_start = pos;
        while (*pos && *pos != ']') pos++;

        if (pos > info_start) {
            size_t info_len = pos - info_start;
            char* info = (char*)malloc(info_len + 1);
            if (info) {
                strncpy(info, info_start, info_len);
                info[info_len] = '\0';
                add_attribute_to_element(parser, citation, "info", info);
                free(info);
            }
        }
    }

    // Find closing ]
    while (*pos && *pos != ']') pos++;
    if (*pos == ']') pos++;

    *text = pos;
    return (Item){.item = (uint64_t)citation};
}

// Check if line is an RST directive (.. directive::) - Only for RST format
static bool is_rst_directive(MarkupParser* parser, const char* line) {
    if (!line || !parser) return false;

    // Only process RST directives in RST format
    if (parser->config.format != MARKUP_RST) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    // Check for ..
    if (*pos != '.' || *(pos+1) != '.' || *(pos+2) != ' ') return false;
    pos += 3;

    // Check for directive name followed by ::
    while (*pos && !isspace(*pos) && *pos != ':') pos++;
    return (*pos == ':' && *(pos+1) == ':');
}

// Parse RST directive (.. code-block:: python) - Enhanced for RST format
static Item parse_rst_directive(MarkupParser* parser, const char* line) {
    Element* directive = create_element(parser, "directive");
    if (!directive) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = line;
    skip_whitespace(&pos);
    pos += 3; // Skip ..

    // Extract directive name
    const char* name_start = pos;
    while (*pos && *pos != ':') pos++;

    size_t name_len = pos - name_start;
    char* name = (char*)malloc(name_len + 1);
    if (name) {
        strncpy(name, name_start, name_len);
        name[name_len] = '\0';
        add_attribute_to_element(parser, directive, "type", name);

        // Add RST-specific attributes based on directive type
        if (strcmp(name, "code-block") == 0 || strcmp(name, "code") == 0) {
            add_attribute_to_element(parser, directive, "category", "code");
        } else if (strcmp(name, "note") == 0 || strcmp(name, "warning") == 0 ||
                   strcmp(name, "danger") == 0 || strcmp(name, "attention") == 0 ||
                   strcmp(name, "caution") == 0 || strcmp(name, "error") == 0 ||
                   strcmp(name, "hint") == 0 || strcmp(name, "important") == 0 ||
                   strcmp(name, "tip") == 0) {
            add_attribute_to_element(parser, directive, "category", "admonition");
        } else if (strcmp(name, "figure") == 0 || strcmp(name, "image") == 0) {
            add_attribute_to_element(parser, directive, "category", "media");
        } else if (strcmp(name, "toctree") == 0 || strcmp(name, "contents") == 0) {
            add_attribute_to_element(parser, directive, "category", "structure");
        } else {
            add_attribute_to_element(parser, directive, "category", "generic");
        }

        free(name);
    }

    // Skip :: and parse arguments
    if (*pos == ':' && *(pos+1) == ':') {
        pos += 2;
        skip_whitespace(&pos);

        if (*pos) {
            add_attribute_to_element(parser, directive, "args", pos);
        }
    }

    parser->current_line++;

    // Parse directive options (lines starting with :option:)
    while (parser->current_line < parser->line_count) {
        const char* option_line = parser->lines[parser->current_line];
        skip_whitespace(&option_line);

        if (*option_line == ':' && strchr(option_line + 1, ':')) {
            // This is an option line like :linenos: or :language: python
            const char* option_start = option_line + 1;
            const char* option_end = strchr(option_start, ':');
            if (option_end) {
                size_t option_name_len = option_end - option_start;
                char* option_name = (char*)malloc(option_name_len + 1);
                if (option_name) {
                    strncpy(option_name, option_start, option_name_len);
                    option_name[option_name_len] = '\0';

                    const char* option_value = option_end + 1;
                    skip_whitespace(&option_value);

                    add_attribute_to_element(parser, directive, option_name,
                                           *option_value ? option_value : "true");
                    free(option_name);
                }
            }
            parser->current_line++;
        } else {
            break;
        }
    }

    // Parse directive content (indented lines)
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];

        // Check if line is indented (part of directive) or empty
        if (is_empty_line(content_line)) {
            if (sb->length > 0) {
                stringbuf_append_char(sb, '\n');
            }
            parser->current_line++;
        } else if (*content_line == ' ' || *content_line == '\t') {
            if (sb->length > 0) {
                stringbuf_append_char(sb, '\n');
            }
            stringbuf_append_str(sb, content_line);
            parser->current_line++;
        } else {
            break;
        }
    }

    // Add content if any
    if (sb->length > 0) {
        String* content_str = stringbuf_to_string(sb);
        Item content_item = {.item = s2it(content_str)};
        list_push((List*)directive, content_item);
        increment_element_content_length(directive);
    }

    return (Item){.item = (uint64_t)directive};
}

// Check if line is an Org block (#+BEGIN_*)
static bool is_org_block(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    return (strncmp(pos, "#+BEGIN_", 8) == 0);
}

// Parse Org block (#+BEGIN_SRC python ... #+END_SRC)
static Item parse_org_block(MarkupParser* parser, const char* line) {
    Element* org_block = create_element(parser, "org-block");
    if (!org_block) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    const char* pos = line;
    skip_whitespace(&pos);
    pos += 8; // Skip #+BEGIN_

    // Extract block type
    const char* type_start = pos;
    while (*pos && !isspace(*pos)) pos++;

    size_t type_len = pos - type_start;
    char* type = (char*)malloc(type_len + 1);
    if (type) {
        strncpy(type, type_start, type_len);
        type[type_len] = '\0';
        add_attribute_to_element(parser, org_block, "type", type);
        free(type);
    }

    // Parse block arguments
    skip_whitespace(&pos);
    if (*pos) {
        add_attribute_to_element(parser, org_block, "args", pos);
    }

    parser->current_line++;

    // Build end marker
    char* end_marker = (char*)malloc(type_len + 10);
    sprintf(end_marker, "#+END_%.*s", (int)type_len, type_start);

    // Collect block content until end marker
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];

        // Check for end marker
        const char* check_pos = content_line;
        skip_whitespace(&check_pos);

        if (strncmp(check_pos, end_marker, strlen(end_marker)) == 0) {
            parser->current_line++; // Skip end marker
            break;
        }

        // Add line to content
        if (sb->length > 0) {
            stringbuf_append_char(sb, '\n');
        }
        stringbuf_append_str(sb, content_line);
        parser->current_line++;
    }

    free(end_marker);

    // Add content
    if (sb->length > 0) {
        String* content_str = stringbuf_to_string(sb);
        Item content_item = {.item = s2it(content_str)};
        list_push((List*)org_block, content_item);
        increment_element_content_length(org_block);
    }

    return (Item){.item = (uint64_t)org_block};
}

// Check if document has YAML frontmatter
static bool has_yaml_frontmatter(MarkupParser* parser) {
    if (!parser || parser->line_count == 0) return false;

    const char* first_line = parser->lines[0];
    skip_whitespace(&first_line);

    return (strcmp(first_line, "---") == 0);
}

// Parse YAML line into key-value pair
static void parse_yaml_line(MarkupParser* parser, const char* line, Element* metadata) {
    // Skip leading whitespace
    while (*line && (*line == ' ' || *line == '\t')) {
        line++;
    }

    // Skip empty lines and comments
    if (!*line || *line == '#') {
        return;
    }

    // Find colon separator
    const char* colon = strchr(line, ':');
    if (!colon) {
        return; // Not a key-value line
    }

    // Extract key
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);
    const char* key_start = line;
    while (key_start < colon) {
        stringbuf_append_char(sb, *key_start);
        key_start++;
    }

    // Trim key
    while (sb->str->len > 0 && (sb->str->chars[sb->str->len-1] == ' ' || sb->str->chars[sb->str->len-1] == '\t')) {
        sb->str->len--;
    }
    sb->str->chars[sb->str->len] = '\0';

    if (sb->str->len == 0) return; // Empty key

    String* key = stringbuf_to_string(sb);

    // Extract value
    const char* value_start = colon + 1;
    while (*value_start && (*value_start == ' ' || *value_start == '\t')) {
        value_start++;
    }

    stringbuf_reset(sb);
    stringbuf_append_str(sb, value_start);

    // Trim trailing whitespace from value
    while (sb->str->len > 0 && (sb->str->chars[sb->str->len-1] == ' ' || sb->str->chars[sb->str->len-1] == '\t' ||
                              sb->str->chars[sb->str->len-1] == '\r' || sb->str->chars[sb->str->len-1] == '\n')) {
        sb->str->len--;
    }
    sb->str->chars[sb->str->len] = '\0';

    String* value = stringbuf_to_string(sb);

    // Remove quotes if present
    if (value && value->len >= 2) {
        if ((value->chars[0] == '"' && value->chars[value->len-1] == '"') ||
            (value->chars[0] == '\'' && value->chars[value->len-1] == '\'')) {
            // Create unquoted version
            stringbuf_reset(sb);
            stringbuf_append_str_n(sb, value->chars + 1, value->len - 2);
            value = stringbuf_to_string(sb);
        }
    }

    // Add as attribute to metadata element
    if (key && key->len > 0 && value && value->len > 0) {
        add_attribute_to_element(parser, metadata, key->chars, value->chars);
    }
}

// Parse YAML frontmatter (---)
static Item parse_yaml_frontmatter(MarkupParser* parser) {
    if (!has_yaml_frontmatter(parser)) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* metadata = create_element(parser, "metadata");
    if (!metadata) {
        return (Item){.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, metadata, "type", "yaml");

    parser->current_line++; // Skip opening ---

    // Parse YAML lines for structured metadata
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Check for closing ---
        const char* pos = line;
        skip_whitespace(&pos);
        if (strcmp(pos, "---") == 0 || strcmp(pos, "...") == 0) {
            parser->current_line++; // Skip closing ---
            break;
        }

        // Parse individual YAML line for key-value pairs
        parse_yaml_line(parser, line, metadata);
        parser->current_line++;
    }

    return (Item){.item = (uint64_t)metadata};
}

// Check if document has Org properties
static bool has_org_properties(MarkupParser* parser) {
    if (!parser || parser->line_count == 0) return false;

    // Check first few lines for #+PROPERTY: or #+TITLE: etc.
    for (int i = 0; i < 10 && i < parser->line_count; i++) {
        const char* line = parser->lines[i];
        skip_whitespace(&line);
        if (strncmp(line, "#+", 2) == 0) {
            return true;
        }
    }

    return false;
}

// Parse Org document properties (#+TITLE:, #+AUTHOR:, etc.)
static Item parse_org_properties(MarkupParser* parser) {
    if (!has_org_properties(parser)) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* properties = create_element(parser, "metadata");
    if (!properties) {
        return (Item){.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, properties, "type", "org");

    // Parse property lines
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];
        const char* pos = line;
        skip_whitespace(&pos);

        if (strncmp(pos, "#+", 2) != 0) {
            break; // No more properties
        }

        pos += 2; // Skip #+
        const char* key_start = pos;

        // Find colon
        while (*pos && *pos != ':') pos++;
        if (*pos != ':') {
            parser->current_line++;
            continue;
        }

        // Extract property key
        size_t key_len = pos - key_start;
        char* key = (char*)malloc(key_len + 1);
        if (key) {
            strncpy(key, key_start, key_len);
            key[key_len] = '\0';

            // Convert to lowercase
            for (int i = 0; key[i]; i++) {
                key[i] = tolower(key[i]);
            }

            pos++; // Skip colon
            skip_whitespace(&pos);

            // Add property as attribute
            if (*pos) {
                add_attribute_to_element(parser, properties, key, pos);
            }

            free(key);
        }

        parser->current_line++;
    }

    return (Item){.item = (uint64_t)properties};
}

// ============================================================================
// MediaWiki-Specific Features
// ============================================================================

// MediaWiki-specific helper functions
static bool is_wiki_heading(const char* line, int* level) {
    if (!line || *line != '=') return false;

    int eq_count = count_leading_chars(line, '=');
    if (eq_count == 0 || eq_count > 6) return false;

    // Check if line ends with same number of =
    int len = strlen(line);
    int trailing_eq = 0;
    for (int i = len - 1; i >= 0 && line[i] == '='; i--) {
        trailing_eq++;
    }

    if (trailing_eq >= eq_count) {
        if (level) *level = eq_count;
        return true;
    }

    return false;
}

static bool is_wiki_list_item(const char* line, char* marker, int* level) {
    if (!line) return false;

    int pos = 0;
    int count = 0;

    // Count leading list markers
    while (line[pos] == '*' || line[pos] == '#' || line[pos] == ':' || line[pos] == ';') {
        if (count == 0) *marker = line[pos]; // First marker determines type
        count++;
        pos++;
    }

    if (count > 0 && (line[pos] == ' ' || line[pos] == '\0')) {
        *level = count;
        return true;
    }

    return false;
}

static bool is_wiki_table_start(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "{|", 2) == 0);
    free(trimmed);
    return result;
}

static bool is_wiki_table_row(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (trimmed[0] == '|' && trimmed[1] != '}' && trimmed[1] != '-');
    free(trimmed);
    return result;
}

static bool is_wiki_table_end(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "|}", 2) == 0);
    free(trimmed);
    return result;
}

static bool is_wiki_horizontal_rule(const char* line) {
    char* trimmed = trim_whitespace(line);
    bool result = (strncmp(trimmed, "----", 4) == 0);
    free(trimmed);
    return result;
}

// MediaWiki-specific block parsers
static Item parse_wiki_table(MarkupParser* parser) {
    const char* line = parser->lines[parser->current_line];
    if (!is_wiki_table_start(line)) return (Item){.item = ITEM_UNDEFINED};

    // Create table element
    Element* table = create_element(parser, "table");
    if (!table) return (Item){.item = ITEM_ERROR};

    parser->current_line++; // Skip {|

    Element* tbody = create_element(parser, "tbody");
    if (!tbody) return (Item){.item = (uint64_t)table};

    Element* current_row = NULL;

    while (parser->current_line < parser->line_count &&
           !is_wiki_table_end(parser->lines[parser->current_line])) {
        const char* line = parser->lines[parser->current_line];

        if (!line || is_empty_line(line)) {
            parser->current_line++;
            continue;
        }

        char* trimmed = trim_whitespace(line);

        if (trimmed[0] == '|' && trimmed[1] == '-') {
            // Table row separator - start new row
            if (current_row) {
                list_push((List*)tbody, (Item){.item = (uint64_t)current_row});
                increment_element_content_length(tbody);
            }
            current_row = create_element(parser, "tr");
        } else if (is_wiki_table_row(line)) {
            // Table cell
            if (!current_row) {
                current_row = create_element(parser, "tr");
            }

            if (current_row) {
                // Parse cell content (skip leading |)
                const char* cell_content = trimmed + 1;
                while (*cell_content == ' ') cell_content++;

                Element* cell = create_element(parser, "td");
                if (cell) {
                    if (strlen(cell_content) > 0) {
                        // Wrap cell content in paragraph for proper block structure
                        Element* paragraph = create_element(parser, "p");
                        if (paragraph) {
                            Item content = parse_inline_spans(parser, cell_content);
                            if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
                                list_push((List*)paragraph, content);
                                increment_element_content_length(paragraph);
                            }
                            list_push((List*)cell, (Item){.item = (uint64_t)paragraph});
                            increment_element_content_length(cell);
                        }
                    }
                    list_push((List*)current_row, (Item){.item = (uint64_t)cell});
                    increment_element_content_length(current_row);
                }
            }
        }

        free(trimmed);
        parser->current_line++;
    }

    // Add final row if exists
    if (current_row) {
        list_push((List*)tbody, (Item){.item = (uint64_t)current_row});
        increment_element_content_length(tbody);
    }

    if (parser->current_line < parser->line_count &&
        is_wiki_table_end(parser->lines[parser->current_line])) {
        parser->current_line++; // Skip |}
    }

    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, (Item){.item = (uint64_t)tbody});
        increment_element_content_length(table);
    }

    return (Item){.item = (uint64_t)table};
}

static Item parse_wiki_list(MarkupParser* parser) {
    const char* line = parser->lines[parser->current_line];
    char marker;
    int level;

    if (!is_wiki_list_item(line, &marker, &level)) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Determine list type
    const char* list_tag = NULL;
    switch (marker) {
        case '*': list_tag = "ul"; break;
        case '#': list_tag = "ol"; break;
        case ':': list_tag = "dl"; break; // Definition list
        case ';': list_tag = "dl"; break; // Definition list
        default: return (Item){.item = ITEM_UNDEFINED};
    }

    Element* list = create_element(parser, list_tag);
    if (!list) return (Item){.item = ITEM_ERROR};

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        if (!line || is_empty_line(line)) {
            parser->current_line++;
            continue;
        }

        char item_marker;
        int item_level;
        if (!is_wiki_list_item(line, &item_marker, &item_level) ||
            item_marker != marker) {
            break;
        }

        // Create list item
        const char* item_tag = (marker == ':' || marker == ';') ? "dd" : "li";
        if (marker == ';') item_tag = "dt"; // Definition term

        Element* list_item = create_element(parser, item_tag);
        if (!list_item) break;

        // Extract item content (skip markers and space)
        const char* content_start = line + item_level;
        if (*content_start == ' ') content_start++;

        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            if (marker == '*' || marker == '#') {
                // Regular lists need paragraph wrapper
                Element* paragraph = create_element(parser, "p");
                if (paragraph) {
                    Item text_content = parse_inline_spans(parser, content);
                    if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                        list_push((List*)paragraph, text_content);
                        increment_element_content_length(paragraph);
                    }
                    list_push((List*)list_item, (Item){.item = (uint64_t)paragraph});
                    increment_element_content_length(list_item);
                }
            } else {
                // Definition lists can have direct content
                Item text_content = parse_inline_spans(parser, content);
                if (text_content.item != ITEM_ERROR && text_content.item != ITEM_UNDEFINED) {
                    list_push((List*)list_item, text_content);
                    increment_element_content_length(list_item);
                }
            }
        }
        free(content);

        list_push((List*)list, (Item){.item = (uint64_t)list_item});
        increment_element_content_length(list);

        parser->current_line++;
    }

    return (Item){.item = (uint64_t)list};
}

// MediaWiki-specific inline parsers
static Item parse_wiki_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (pos[0] != '[' || pos[1] != '[') return (Item){.item = ITEM_UNDEFINED};

    pos += 2; // Skip [[

    const char* link_start = pos;
    const char* link_end = NULL;
    const char* display_start = NULL;
    const char* display_end = NULL;

    // Find closing ]]
    while (*pos != '\0' && pos[1] != '\0') {
        if (pos[0] == ']' && pos[1] == ']') {
            if (display_start == NULL) {
                link_end = pos;
            } else {
                display_end = pos;
            }
            pos += 2;
            break;
        } else if (*pos == '|' && display_start == NULL) {
            link_end = pos;
            pos++;
            display_start = pos;
        } else {
            pos++;
        }
    }

    if (link_end == NULL) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* link_elem = create_element(parser, "a");
    if (!link_elem) return (Item){.item = ITEM_ERROR};

    // Extract link target
    int link_len = link_end - link_start;
    char* link_target = (char*)malloc(link_len + 1);
    strncpy(link_target, link_start, link_len);
    link_target[link_len] = '\0';
    add_attribute_to_element(parser, link_elem, "href", link_target);

    // Extract display text (or use link target)
    char* display_text;
    if (display_start != NULL && display_end != NULL) {
        int display_len = display_end - display_start;
        display_text = (char*)malloc(display_len + 1);
        strncpy(display_text, display_start, display_len);
        display_text[display_len] = '\0';
    } else {
        display_text = strdup(link_target);
    }

    if (strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, (Item){.item = s2it(text_str)});
            increment_element_content_length(link_elem);
        }
    }

    free(link_target);
    free(display_text);
    *text = pos;
    return (Item){.item = (uint64_t)link_elem};
}

static Item parse_wiki_external_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (*pos != '[') return (Item){.item = ITEM_UNDEFINED};

    pos++; // Skip [

    const char* url_start = pos;
    const char* url_end = NULL;
    const char* display_start = NULL;
    const char* display_end = NULL;

    // Find space or closing ]
    while (*pos != '\0') {
        if (*pos == ']') {
            if (display_start == NULL) {
                url_end = pos;
            } else {
                display_end = pos;
            }
            pos++;
            break;
        } else if (*pos == ' ' && display_start == NULL) {
            url_end = pos;
            pos++;
            display_start = pos;
        } else {
            pos++;
        }
    }

    if (url_end == NULL) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* link_elem = create_element(parser, "a");
    if (!link_elem) return (Item){.item = ITEM_ERROR};

    // Extract URL
    int url_len = url_end - url_start;
    char* url = (char*)malloc(url_len + 1);
    strncpy(url, url_start, url_len);
    url[url_len] = '\0';
    add_attribute_to_element(parser, link_elem, "href", url);

    // Extract display text (or use URL)
    char* display_text;
    if (display_start != NULL && display_end != NULL) {
        int display_len = display_end - display_start;
        display_text = (char*)malloc(display_len + 1);
        strncpy(display_text, display_start, display_len);
        display_text[display_len] = '\0';
    } else {
        display_text = strdup(url);
    }

    if (strlen(display_text) > 0) {
        String* text_str = create_string(parser, display_text);
        if (text_str) {
            list_push((List*)link_elem, (Item){.item = s2it(text_str)});
            increment_element_content_length(link_elem);
        }
    }

    free(url);
    free(display_text);
    *text = pos;
    return (Item){.item = (uint64_t)link_elem};
}

static Item parse_wiki_bold_italic(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (*pos != '\'') return (Item){.item = ITEM_UNDEFINED};

    const char* start_pos = pos;
    int quote_count = 0;

    // Count opening quotes
    while (*pos == '\'') {
        quote_count++;
        pos++;
    }

    if (quote_count < 2) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    const char* content_start = pos;
    const char* content_end = NULL;

    // Find closing quotes
    while (*pos != '\0') {
        if (*pos == '\'') {
            int close_quote_count = 0;
            const char* temp_pos = pos;

            while (*temp_pos == '\'') {
                close_quote_count++;
                temp_pos++;
            }

            if (close_quote_count >= quote_count) {
                content_end = pos;
                pos += quote_count;
                break;
            }

            // If we found quotes but not enough, skip past all the quotes we counted
            pos = temp_pos;
        } else {
            pos++;
        }
    }

    if (content_end == NULL) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Determine element type
    const char* tag_name;
    if (quote_count >= 5) {
        tag_name = "strong"; // Bold + italic, but we'll use strong for now
    } else if (quote_count >= 3) {
        tag_name = "strong"; // Bold
    } else {
        tag_name = "em"; // Italic
    }

    Element* format_elem = create_element(parser, tag_name);
    if (!format_elem) return (Item){.item = ITEM_ERROR};

    // Extract content
    int content_len = content_end - content_start;
    if (content_len < 0) return (Item){.item = ITEM_ERROR};

    char* content = (char*)malloc(content_len + 1);
    if (!content) return (Item){.item = ITEM_ERROR};

    strncpy(content, content_start, content_len);
    content[content_len] = '\0';

    if (strlen(content) > 0) {
        String* text_str = create_string(parser, content);
        if (text_str) {
            list_push((List*)format_elem, (Item){.item = s2it(text_str)});
            increment_element_content_length(format_elem);
        }
    }

    free(content);
    *text = pos;
    return (Item){.item = (uint64_t)format_elem};
}

// Parse wiki template ({{template|arg1|arg2}})
static Item parse_wiki_template(MarkupParser* parser, const char** text) {
    const char* pos = *text;

    // Check for {{
    if (*pos != '{' || *(pos+1) != '{') {
        return (Item){.item = ITEM_UNDEFINED};
    }

    const char* start_pos = pos;
    pos += 2; // Skip {{
    const char* template_start = pos;

    // Find closing }} by tracking double-brace pairs
    int double_brace_depth = 1; // We've seen one opening {{
    const char* content_end = NULL;

    while (*pos && double_brace_depth > 0) {
        if (*pos == '{' && *(pos+1) == '{') {
            double_brace_depth++;
            pos += 2;
        } else if (*pos == '}' && *(pos+1) == '}') {
            double_brace_depth--;
            if (double_brace_depth == 0) {
                content_end = pos;
                pos += 2; // Skip the closing }}
                break;
            } else {
                pos += 2;
            }
        } else {
            pos++;
        }

        // Safety check to prevent infinite loops
        if (pos - start_pos > 10000) {
            *text = start_pos + 2; // Advance past {{ to prevent infinite loop
            return (Item){.item = ITEM_UNDEFINED};
        }
    }

    if (!content_end || double_brace_depth != 0) {
        // Advance past the {{ to prevent infinite loop
        *text = start_pos + 2;
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* template_elem = create_element(parser, "wiki-template");
    if (!template_elem) {
        *text = pos;
        return (Item){.item = ITEM_ERROR};
    }

    // Extract template content
    size_t content_len = content_end - template_start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, template_start, content_len);
        content[content_len] = '\0';

        // Parse template name and arguments
        char* pipe_pos = strchr(content, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';
            add_attribute_to_element(parser, template_elem, "name", content);
            add_attribute_to_element(parser, template_elem, "args", pipe_pos + 1);
        } else {
            add_attribute_to_element(parser, template_elem, "name", content);
        }

        free(content);
    }

    *text = pos;
    return (Item){.item = (uint64_t)template_elem};
}

// ============================================================================
// RST-Specific Features Missing from New Parser (Added from old input-rst.cpp)
// ============================================================================

// RST Transition lines (horizontal rules with ----)
static bool is_rst_transition_line(const char* line) {
    if (!line || strlen(line) < 4) return false;

    int dash_count = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '-') {
            dash_count++;
        } else if (!isspace(line[i])) {
            return false;
        }
    }

    return dash_count >= 4;
}

static Item parse_rst_transition(MarkupParser* parser) {
    Element* hr = create_element(parser, "hr");
    parser->current_line++;
    return (Item){.item = (uint64_t)hr};
}

// RST Definition Lists
static bool is_rst_definition_list_item(const char* line) {
    if (!line || is_empty_line(line)) return false;

    // definition term should not start with whitespace
    if (isspace(line[0])) return false;

    // should contain text
    for (int i = 0; line[i]; i++) {
        if (!isspace(line[i])) return true;
    }

    return false;
}

static bool is_rst_definition_list_definition(const char* line) {
    if (!line) return false;

    // definition should start with indentation
    return isspace(line[0]) && !is_empty_line(line);
}

static Item parse_rst_definition_list(MarkupParser* parser) {
    Element* def_list = create_element(parser, "dl");
    if (!def_list) return (Item){.item = ITEM_ERROR};

    while (parser->current_line < parser->line_count &&
           is_rst_definition_list_item(parser->lines[parser->current_line])) {
        const char* term_line = parser->lines[parser->current_line];

        // create definition term
        Element* dt = create_element(parser, "dt");
        if (!dt) break;

        char* term_content = trim_whitespace(term_line);
        if (term_content && strlen(term_content) > 0) {
            Item term_text = parse_inline_content(parser, term_content);
            if (term_text.item != ITEM_UNDEFINED) {
                list_push((List*)dt, term_text);
                increment_element_content_length(dt);
            }
        }
        free(term_content);

        list_push((List*)def_list, (Item){.item = (uint64_t)dt});
        increment_element_content_length(def_list);

        parser->current_line++;

        // parse definition(s)
        while (parser->current_line < parser->line_count &&
               is_rst_definition_list_definition(parser->lines[parser->current_line])) {
            const char* def_line = parser->lines[parser->current_line];

            Element* dd = create_element(parser, "dd");
            if (!dd) break;

            char* def_content = trim_whitespace(def_line);
            if (def_content && strlen(def_content) > 0) {
                Item def_text = parse_inline_content(parser, def_content);
                if (def_text.item != ITEM_UNDEFINED) {
                    list_push((List*)dd, def_text);
                    increment_element_content_length(dd);
                }
            }
            free(def_content);

            list_push((List*)def_list, (Item){.item = (uint64_t)dd});
            increment_element_content_length(def_list);

            parser->current_line++;
        }
    }

    return (Item){.item = (uint64_t)def_list};
}

// RST Literal Blocks (:: marker)
static bool is_rst_literal_block_marker(const char* line) {
    if (!line) return false;

    char* trimmed = trim_whitespace(line);
    bool is_marker = (strlen(trimmed) == 2 && strcmp(trimmed, "::") == 0);
    free(trimmed);
    return is_marker;
}

static bool line_ends_with_double_colon(const char* line) {
    if (!line) return false;

    char* trimmed = trim_whitespace(line);
    int len = strlen(trimmed);
    bool ends_with = len >= 2 && trimmed[len-2] == ':' && trimmed[len-1] == ':';
    free(trimmed);
    return ends_with;
}

static int count_leading_spaces(const char* str) {
    int count = 0;
    while (str[count] == ' ') count++;
    return count;
}

static Item parse_rst_literal_block(MarkupParser* parser) {
    // literal block starts with :: on its own line or at end of paragraph
    const char* line = parser->lines[parser->current_line];

    bool is_marker_line = is_rst_literal_block_marker(line);
    bool ends_with_double_colon = line_ends_with_double_colon(line);

    if (!is_marker_line && !ends_with_double_colon) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Create code element directly (following updated schema - no pre wrapper)
    Element* code_block = create_element(parser, "code");
    if (!code_block) return (Item){.item = ITEM_ERROR};

    parser->current_line++;

    // collect literal content
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);
    bool first_line = true;
    int base_indent = -1;

    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];

        if (is_empty_line(content_line)) {
            if (!first_line) {
                stringbuf_append_char(sb, '\n');
            }
            first_line = false;
            parser->current_line++;
            continue;
        }

        int indent = count_leading_spaces(content_line);

        // first non-empty line sets base indentation
        if (base_indent == -1) {
            base_indent = indent;
        }

        // if line is not indented more than base, end literal block
        if (indent < base_indent) {
            break;
        }

        // add line to content
        if (!first_line) {
            stringbuf_append_char(sb, '\n');
        }

        // add content with base indentation removed
        const char* content_start = content_line + base_indent;
        stringbuf_append_str(sb, content_start);

        first_line = false;
        parser->current_line++;
    }

    // create string content
    if (sb->length > 0) {
        String* content_str = stringbuf_to_string(sb);
        Item content_item = {.item = s2it(content_str)};
        list_push((List*)code_block, content_item);
        increment_element_content_length(code_block);
    }

    return (Item){.item = (uint64_t)code_block};
}

// RST Comments (.. comment)
static bool is_rst_comment_line(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);
    return pos[0] == '.' && pos[1] == '.' &&
           (pos[2] == ' ' || pos[2] == '\t' || pos[2] == '\0');
}

static Item parse_rst_comment(MarkupParser* parser) {
    if (!is_rst_comment_line(parser->lines[parser->current_line])) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* comment = create_element(parser, "comment");
    if (!comment) return (Item){.item = ITEM_ERROR};

    const char* line = parser->lines[parser->current_line];
    const char* pos = line;
    skip_whitespace(&pos);
    pos += 2; // skip ..
    skip_whitespace(&pos);

    char* content = trim_whitespace(pos);
    if (content && strlen(content) > 0) {
        String* comment_text = create_string(parser, content);
        if (comment_text) {
            list_push((List*)comment, (Item){.item = s2it(comment_text)});
            increment_element_content_length(comment);
        }
    }
    free(content);

    parser->current_line++;
    return (Item){.item = (uint64_t)comment};
}

// RST Grid Tables (complex table format)
static bool is_rst_grid_table_line(const char* line) {
    if (!line || strlen(line) < 3) return false;

    // grid table lines contain + and - or | characters
    bool has_plus = false;
    bool has_dash_or_pipe = false;

    for (int i = 0; line[i]; i++) {
        if (line[i] == '+') {
            has_plus = true;
        } else if (line[i] == '-' || line[i] == '|') {
            has_dash_or_pipe = true;
        } else if (!isspace(line[i])) {
            return false;
        }
    }

    return has_plus && has_dash_or_pipe;
}

static Item parse_rst_grid_table(MarkupParser* parser) {
    // Grid table parsing would be complex, for now create a basic table
    Element* table = create_element(parser, "table");
    if (!table) return (Item){.item = ITEM_ERROR};

    add_attribute_to_element(parser, table, "type", "grid");

    // Skip grid table lines for now (basic implementation)
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];
        if (is_rst_grid_table_line(line) || is_empty_line(line)) {
            parser->current_line++;
        } else {
            break;
        }
    }

    return (Item){.item = (uint64_t)table};
}

// Enhanced RST inline parsing for double backticks and trailing underscores
static Item parse_rst_double_backtick_literal(MarkupParser* parser, const char** text) {
    if (**text != '`' || *(*text + 1) != '`') {
        return (Item){.item = ITEM_UNDEFINED};
    }

    const char* start = *text + 2; // skip opening ``
    const char* pos = start;
    const char* end = NULL;

    // find closing ``
    while (*pos != '\0' && *(pos + 1) != '\0') {
        if (*pos == '`' && *(pos + 1) == '`') {
            end = pos;
            break;
        }
        pos++;
    }

    if (!end) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    Element* code_elem = create_element(parser, "code");
    if (!code_elem) {
        *text = pos + 2;
        return (Item){.item = ITEM_ERROR};
    }

    // extract content between markers
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, start, content_len);
        content[content_len] = '\0';

        String* code_str = create_string(parser, content);
        if (code_str) {
            list_push((List*)code_elem, (Item){.item = s2it(code_str)});
            increment_element_content_length(code_elem);
        }
        free(content);
    }

    *text = end + 2; // skip closing ``
    return (Item){.item = (uint64_t)code_elem};
}

static Item parse_rst_trailing_underscore_reference(MarkupParser* parser, const char** text) {
    if (**text != '_') return (Item){.item = ITEM_UNDEFINED};

    // work backwards to find start of reference
    const char* current = *text;
    const char* ref_start = current - 1;

    while (ref_start > parser->lines[parser->current_line] && !isspace(*(ref_start - 1))) {
        ref_start--;
    }

    if (ref_start >= current) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // extract reference text
    size_t ref_len = current - ref_start;
    char* ref_text = (char*)malloc(ref_len + 1);
    if (!ref_text) {
        (*text)++;
        return (Item){.item = ITEM_ERROR};
    }

    strncpy(ref_text, ref_start, ref_len);
    ref_text[ref_len] = '\0';

    Element* ref_elem = create_element(parser, "a");
    if (!ref_elem) {
        free(ref_text);
        (*text)++;
        return (Item){.item = ITEM_ERROR};
    }

    add_attribute_to_element(parser, ref_elem, "href", ref_text);

    String* link_text = create_string(parser, ref_text);
    if (link_text) {
        list_push((List*)ref_elem, (Item){.item = s2it(link_text)});
        increment_element_content_length(ref_elem);
    }

    free(ref_text);
    (*text)++; // skip _
    return (Item){.item = (uint64_t)ref_elem};
}

// ===========================================
// Textile-specific parser functions
// ===========================================

// Helper function to check if line is a Textile heading (h1. to h6.)
static bool is_textile_heading(const char* line, int* level) {
    if (!line || strlen(line) < 3) return false;

    // Check for h1. to h6. patterns
    if (line[0] == 'h' && line[1] >= '1' && line[1] <= '6' && line[2] == '.') {
        if (level) *level = line[1] - '0';
        return true;
    }
    return false;
}

// Helper function to check if line is a Textile list item
static bool is_textile_list_item(const char* line, char* list_type) {
    if (!line) return false;

    // Skip leading whitespace to check for indentation
    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t') indent++;

    if (line[indent] == '*' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        if (list_type) *list_type = '*'; // bulleted list
        return true;
    }
    if (line[indent] == '#' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        if (list_type) *list_type = '#'; // numbered list
        return true;
    }
    if (line[indent] == '-' && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        // Check if it's a definition list (has := in the line)
        if (strstr(line, ":=")) {
            if (list_type) *list_type = '-'; // definition list
            return true;
        }
    }
    return false;
}

// Helper functions for Textile block elements
static bool is_textile_block_code(const char* line) {
    return strncmp(line, "bc.", 3) == 0 || strncmp(line, "bc..", 4) == 0;
}

static bool is_textile_block_quote(const char* line) {
    return strncmp(line, "bq.", 3) == 0 || strncmp(line, "bq..", 4) == 0;
}

static bool is_textile_pre(const char* line) {
    return strncmp(line, "pre.", 4) == 0 || strncmp(line, "pre..", 5) == 0;
}

static bool is_textile_comment(const char* line) {
    return strncmp(line, "###.", 4) == 0;
}

static bool is_textile_notextile(const char* line) {
    return strncmp(line, "notextile.", 10) == 0 || strncmp(line, "notextile..", 11) == 0;
}

// Parse Textile modifiers (class, style, etc.)
static char* parse_textile_modifiers(const char* line, int* start_pos) {
    // Parse Textile formatting modifiers like (class), {style}, [lang], <>, etc.
    // This is a simplified version - full implementation would be more complex
    int pos = *start_pos;

    // Skip the block signature (e.g., "h1.", "p.", etc.)
    while (line[pos] && line[pos] != '.' && !isspace(line[pos])) pos++;
    if (line[pos] == '.') pos++; // Skip the dot

    // Look for modifiers
    char* modifiers = NULL;
    int mod_len = 0;

    while (line[pos] && !isalnum(line[pos])) {
        if (line[pos] == '(' || line[pos] == '{' || line[pos] == '[' ||
            line[pos] == '<' || line[pos] == '>' || line[pos] == '=') {
            // Found modifier start, collect until we find the content
            int mod_start = pos;
            while (line[pos] && line[pos] != ' ') pos++;
            mod_len = pos - mod_start;
            if (mod_len > 0) {
                modifiers = (char*)malloc(mod_len + 1);
                strncpy(modifiers, line + mod_start, mod_len);
                modifiers[mod_len] = '\0';
            }
            break;
        }
        pos++;
    }

    // Skip to content
    while (line[pos] && isspace(line[pos])) pos++;
    *start_pos = pos;

    return modifiers;
}

// Parse Textile inline content (emphasis, strong, code, etc.)
static Item parse_textile_inline_content(MarkupParser* parser, const char* text) {
    if (!text || strlen(text) == 0) {
        return (Item){.item = s2it(create_string(parser, ""))};
    }

    // Create a container element for mixed content
    Element* container = create_element(parser, "span");
    if (!container) return (Item){.item = ITEM_NULL};

    const char* ptr = text;
    const char* start = text;

    while (*ptr) {
        bool found_markup = false;

        // Check for various inline formatting
        if (*ptr == '*' && *(ptr + 1) == '*') {
            // **bold**
            const char* end = strstr(ptr + 2, "**");
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add bold element
                Element* bold = create_element(parser, "strong");
                char* bold_text = (char*)malloc(end - (ptr + 2) + 1);
                strncpy(bold_text, ptr + 2, end - (ptr + 2));
                bold_text[end - (ptr + 2)] = '\0';
                String* bold_str = create_string(parser, bold_text);
                list_push((List*)bold, (Item){.item = s2it(bold_str)});
                increment_element_content_length(bold);
                list_push((List*)container, (Item){.item = (uint64_t)bold});
                increment_element_content_length(container);
                free(bold_text);

                ptr = end + 2;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '*') {
            // *strong*
            const char* end = strchr(ptr + 1, '*');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add strong element
                Element* strong = create_element(parser, "strong");
                char* strong_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(strong_text, ptr + 1, end - (ptr + 1));
                strong_text[end - (ptr + 1)] = '\0';
                String* strong_str = create_string(parser, strong_text);
                list_push((List*)strong, (Item){.item = s2it(strong_str)});
                increment_element_content_length(strong);
                list_push((List*)container, (Item){.item = (uint64_t)strong});
                increment_element_content_length(container);
                free(strong_text);

                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '_') {
            // _emphasis_
            const char* end = strchr(ptr + 1, '_');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add emphasis element
                Element* em = create_element(parser, "em");
                char* em_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(em_text, ptr + 1, end - (ptr + 1));
                em_text[end - (ptr + 1)] = '\0';
                String* em_str = create_string(parser, em_text);
                list_push((List*)em, (Item){.item = s2it(em_str)});
                increment_element_content_length(em);
                list_push((List*)container, (Item){.item = (uint64_t)em});
                increment_element_content_length(container);
                free(em_text);

                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '@') {
            // @code@
            const char* end = strchr(ptr + 1, '@');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add code element
                Element* code = create_element(parser, "code");
                char* code_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(code_text, ptr + 1, end - (ptr + 1));
                code_text[end - (ptr + 1)] = '\0';
                String* code_str = create_string(parser, code_text);
                list_push((List*)code, (Item){.item = s2it(code_str)});
                increment_element_content_length(code);
                list_push((List*)container, (Item){.item = (uint64_t)code});
                increment_element_content_length(container);
                free(code_text);

                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '^') {
            // ^superscript^
            const char* end = strchr(ptr + 1, '^');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add superscript element
                Element* sup = create_element(parser, "sup");
                char* sup_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sup_text, ptr + 1, end - (ptr + 1));
                sup_text[end - (ptr + 1)] = '\0';
                String* sup_str = create_string(parser, sup_text);
                list_push((List*)sup, (Item){.item = s2it(sup_str)});
                increment_element_content_length(sup);
                list_push((List*)container, (Item){.item = (uint64_t)sup});
                increment_element_content_length(container);
                free(sup_text);

                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        } else if (*ptr == '~') {
            // ~subscript~
            const char* end = strchr(ptr + 1, '~');
            if (end) {
                // Add preceding text
                if (ptr > start) {
                    char* before = (char*)malloc(ptr - start + 1);
                    strncpy(before, start, ptr - start);
                    before[ptr - start] = '\0';
                    String* before_str = create_string(parser, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }

                // Add subscript element
                Element* sub = create_element(parser, "sub");
                char* sub_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sub_text, ptr + 1, end - (ptr + 1));
                sub_text[end - (ptr + 1)] = '\0';
                String* sub_str = create_string(parser, sub_text);
                list_push((List*)sub, (Item){.item = s2it(sub_str)});
                increment_element_content_length(sub);
                list_push((List*)container, (Item){.item = (uint64_t)sub});
                increment_element_content_length(container);
                free(sub_text);

                ptr = end + 1;
                start = ptr;
                found_markup = true;
            }
        }

        if (!found_markup) {
            ptr++;
        }
    }

    // Add any remaining text
    if (ptr > start) {
        char* remaining = (char*)malloc(ptr - start + 1);
        strncpy(remaining, start, ptr - start);
        remaining[ptr - start] = '\0';
        String* remaining_str = create_string(parser, remaining);
        list_push((List*)container, (Item){.item = s2it(remaining_str)});
        increment_element_content_length(container);
        free(remaining);
    }

    return (Item){.item = (uint64_t)container};
}

// Parse Textile code block (bc. or bc..)
static Item parse_textile_code_block(MarkupParser* parser, const char* line) {
    Element* code_block = create_element(parser, "pre");
    if (!code_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    bool extended = strncmp(line, "bc..", 4) == 0;
    add_attribute_to_element(parser, code_block, "extended", extended ? "true" : "false");

    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser, code_block, "modifiers", modifiers);
        free(modifiers);
    }

    const char* content = line + start_pos;
    String* code_content = create_string(parser, content);
    list_push((List*)code_block, (Item){.item = s2it(code_content)});
    increment_element_content_length(code_block);

    parser->current_line++;

    // For extended blocks, collect until we find another block signature
    if (extended) {
        while (parser->current_line < parser->line_count) {
            const char* next_line = parser->lines[parser->current_line];

            // Check if this line starts a new block
            if (is_textile_heading(next_line, NULL) || is_textile_block_code(next_line) ||
                is_textile_block_quote(next_line) || is_textile_pre(next_line) ||
                strncmp(next_line, "p.", 2) == 0) {
                break;
            }

            String* line_content = create_string(parser, next_line);
            list_push((List*)code_block, (Item){.item = s2it(line_content)});
            increment_element_content_length(code_block);
            parser->current_line++;
        }
    }

    return (Item){.item = (uint64_t)code_block};
}

// Parse Textile block quote (bq. or bq..)
static Item parse_textile_block_quote(MarkupParser* parser, const char* line) {
    Element* quote_block = create_element(parser, "blockquote");
    if (!quote_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    bool extended = strncmp(line, "bq..", 4) == 0;
    add_attribute_to_element(parser, quote_block, "extended", extended ? "true" : "false");

    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser, quote_block, "modifiers", modifiers);
        free(modifiers);
    }

    const char* content = line + start_pos;
    Item inline_content = parse_textile_inline_content(parser, content);
    list_push((List*)quote_block, inline_content);
    increment_element_content_length(quote_block);

    parser->current_line++;

    // For extended blocks, collect until we find another block signature
    if (extended) {
        while (parser->current_line < parser->line_count) {
            const char* next_line = parser->lines[parser->current_line];

            // Check if this line starts a new block
            if (is_textile_heading(next_line, NULL) || is_textile_block_code(next_line) ||
                is_textile_block_quote(next_line) || is_textile_pre(next_line) ||
                strncmp(next_line, "p.", 2) == 0) {
                break;
            }

            Item line_content = parse_textile_inline_content(parser, next_line);
            list_push((List*)quote_block, line_content);
            increment_element_content_length(quote_block);
            parser->current_line++;
        }
    }

    return (Item){.item = (uint64_t)quote_block};
}

// Parse Textile pre-formatted block (pre. or pre..)
static Item parse_textile_pre_block(MarkupParser* parser, const char* line) {
    Element* pre_block = create_element(parser, "pre");
    if (!pre_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser, pre_block, "modifiers", modifiers);
        free(modifiers);
    }

    const char* content = line + start_pos;
    String* pre_content = create_string(parser, content);
    list_push((List*)pre_block, (Item){.item = s2it(pre_content)});
    increment_element_content_length(pre_block);

    parser->current_line++;
    return (Item){.item = (uint64_t)pre_block};
}

// Parse Textile comment (###.)
static Item parse_textile_comment(MarkupParser* parser, const char* line) {
    Element* comment = create_element(parser, "!--");  // HTML comment style
    if (!comment) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    const char* content = line + 4; // Skip "###."
    String* comment_content = create_string(parser, content);
    list_push((List*)comment, (Item){.item = s2it(comment_content)});
    increment_element_content_length(comment);

    parser->current_line++;
    return (Item){.item = (uint64_t)comment};
}

// Parse Textile notextile block
static Item parse_textile_notextile(MarkupParser* parser, const char* line) {
    Element* notextile = create_element(parser, "notextile");
    if (!notextile) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    bool extended = strncmp(line, "notextile..", 11) == 0;
    const char* content = extended ? line + 11 : line + 10;
    while (*content && isspace(*content)) content++;

    String* raw_content = create_string(parser, content);
    list_push((List*)notextile, (Item){.item = s2it(raw_content)});
    increment_element_content_length(notextile);
    add_attribute_to_element(parser, notextile, "extended", extended ? "true" : "false");

    parser->current_line++;
    return (Item){.item = (uint64_t)notextile};
}

// Parse Textile list item
static Item parse_textile_list_item(MarkupParser* parser, const char* line) {
    char list_type = 0;
    if (!is_textile_list_item(line, &list_type)) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    Element* list_item = create_element(parser, "li");
    if (!list_item) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }

    const char* type_str = (list_type == '*') ? "bulleted" :
                          (list_type == '#') ? "numbered" :
                          (list_type == '-') ? "definition" : "unknown";
    add_attribute_to_element(parser, list_item, "type", type_str);

    // Find the content after the list marker
    const char* content = line;
    while (*content && (*content == ' ' || *content == '\t')) content++;
    content++; // Skip the marker (* # -)
    while (*content && (*content == ' ' || *content == '\t')) content++;

    if (list_type == '-') {
        // Definition list - split on ":="
        const char* def_sep = strstr(content, ":=");
        if (def_sep) {
            // Term
            char* term = (char*)malloc(def_sep - content + 1);
            strncpy(term, content, def_sep - content);
            term[def_sep - content] = '\0';
            char* trimmed_term = trim_whitespace(term);
            String* term_str = create_string(parser, trimmed_term);

            Element* term_elem = create_element(parser, "dt");
            list_push((List*)term_elem, (Item){.item = s2it(term_str)});
            increment_element_content_length(term_elem);
            list_push((List*)list_item, (Item){.item = (uint64_t)term_elem});
            increment_element_content_length(list_item);

            free(term);
            free(trimmed_term);

            // Definition
            const char* definition = def_sep + 2;
            while (*definition && isspace(*definition)) definition++;

            Element* def_elem = create_element(parser, "dd");
            Item def_content = parse_textile_inline_content(parser, definition);
            list_push((List*)def_elem, def_content);
            increment_element_content_length(def_elem);
            list_push((List*)list_item, (Item){.item = (uint64_t)def_elem});
            increment_element_content_length(list_item);
        }
    } else {
        // Regular list item
        Item item_content = parse_textile_inline_content(parser, content);
        list_push((List*)list_item, item_content);
        increment_element_content_length(list_item);
    }

    parser->current_line++;
    return (Item){.item = (uint64_t)list_item};
}

// ============================================================================
// AsciiDoc-Specific Features (from old input-adoc.cpp)
// ============================================================================

// AsciiDoc heading detection (= Header, == Header, etc.)
static bool is_asciidoc_heading(const char* line, int* level) {
    if (!line || !level) return false;

    *level = 0;
    const char* pos = line;
    skip_whitespace(&pos);

    // Count leading equals signs
    while (*pos == '=' && *level < 6) {
        (*level)++;
        pos++;
    }

    if (*level == 0) return false;

    // Must be followed by space or end of line
    return (*pos == '\0' || *pos == ' ' || *pos == '\t');
}

// AsciiDoc list item detection (* Item)
static bool is_asciidoc_list_item(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);
    return (*pos == '*' && (*(pos+1) == ' ' || *(pos+1) == '\t'));
}

// AsciiDoc listing block detection (----)
static bool is_asciidoc_listing_block(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);
    return (strncmp(pos, "----", 4) == 0 && strlen(pos) >= 4);
}

// AsciiDoc admonition detection (NOTE:, WARNING:, etc.)
static bool is_asciidoc_admonition(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);

    return (strncmp(pos, "NOTE:", 5) == 0 ||
            strncmp(pos, "TIP:", 4) == 0 ||
            strncmp(pos, "IMPORTANT:", 10) == 0 ||
            strncmp(pos, "WARNING:", 8) == 0 ||
            strncmp(pos, "CAUTION:", 8) == 0);
}

// AsciiDoc table start detection (|===)
static bool is_asciidoc_table_start(const char* line) {
    if (!line) return false;

    const char* pos = line;
    skip_whitespace(&pos);
    return (strncmp(pos, "|===", 4) == 0);
}

// Parse AsciiDoc heading
static Item parse_asciidoc_heading(MarkupParser* parser, const char* line) {
    int level;
    if (!is_asciidoc_heading(line, &level)) {
        return parse_paragraph(parser, line);
    }

    // Create header element
    char tag_name[10];
    snprintf(tag_name, sizeof(tag_name), "h%d", level);
    Element* header = create_element(parser, tag_name);
    if (!header) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }

    // Add level attribute (required by schema)
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(parser, header, "level", level_str);

    // Parse content after equals signs
    const char* pos = line;
    skip_whitespace(&pos);
    while (*pos == '=') pos++; // Skip equals signs
    skip_whitespace(&pos);

    if (*pos) {
        String* content = create_string(parser, pos);
        list_push((List*)header, (Item){.item = s2it(content)});
        increment_element_content_length(header);
    }

    parser->current_line++;
    return (Item){.item = (uint64_t)header};
}

// Parse AsciiDoc list
static Item parse_asciidoc_list(MarkupParser* parser) {
    Element* list = create_element(parser, "ul");
    if (!list) return (Item){.item = ITEM_ERROR};

    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        if (!is_asciidoc_list_item(line)) {
            break;
        }

        // Create list item
        Element* list_item = create_element(parser, "li");
        if (!list_item) {
            parser->current_line++;
            continue;
        }

        // Parse content after asterisk
        const char* pos = line;
        skip_whitespace(&pos);
        if (*pos == '*') pos++; // Skip asterisk
        skip_whitespace(&pos);

        if (*pos) {
            Element* paragraph = create_element(parser, "p");
            if (paragraph) {
                Item content = parse_asciidoc_inline(parser, pos);
                if (content.item != ITEM_UNDEFINED) {
                    list_push((List*)paragraph, content);
                    increment_element_content_length(paragraph);
                }
                list_push((List*)list_item, (Item){.item = (uint64_t)paragraph});
                increment_element_content_length(list_item);
            }
        }

        list_push((List*)list, (Item){.item = (uint64_t)list_item});
        increment_element_content_length(list);
        parser->current_line++;
    }

    return (Item){.item = (uint64_t)list};
}

// Parse AsciiDoc listing block (----)
static Item parse_asciidoc_listing_block(MarkupParser* parser) {
    parser->current_line++; // Skip opening ----

    // Find closing ----
    int end_line = -1;
    for (int i = parser->current_line; i < parser->line_count; i++) {
        if (is_asciidoc_listing_block(parser->lines[i])) {
            end_line = i;
            break;
        }
    }

    if (end_line == -1) {
        // No closing delimiter, treat as regular paragraph
        parser->current_line--;
        return parse_paragraph(parser, parser->lines[parser->current_line]);
    }

    // Create pre and code blocks
    Element* pre_block = create_element(parser, "pre");
    if (!pre_block) return (Item){.item = ITEM_ERROR};

    Element* code_block = create_element(parser, "code");
    if (!code_block) return (Item){.item = ITEM_ERROR};

    // Concatenate content lines
    if (end_line > parser->current_line) {
        size_t total_len = 0;
        for (int i = parser->current_line; i < end_line; i++) {
            total_len += strlen(parser->lines[i]) + 1; // +1 for newline
        }

        char* content = (char*)malloc(total_len + 1);
        content[0] = '\0';

        for (int i = parser->current_line; i < end_line; i++) {
            strcat(content, parser->lines[i]);
            if (i < end_line - 1) strcat(content, "\n");
        }

        String* content_str = create_string(parser, content);
        if (content_str) {
            list_push((List*)code_block, (Item){.item = s2it(content_str)});
            increment_element_content_length(code_block);
        }

        free(content);
    }

    // Add code block to pre block
    list_push((List*)pre_block, (Item){.item = (uint64_t)code_block});
    increment_element_content_length(pre_block);

    parser->current_line = end_line + 1; // Skip closing ----
    return (Item){.item = (uint64_t)pre_block};
}

// Parse AsciiDoc admonition
static Item parse_asciidoc_admonition(MarkupParser* parser, const char* line) {
    Element* admonition = create_element(parser, "div");
    if (!admonition) return (Item){.item = ITEM_ERROR};

    const char* pos = line;
    skip_whitespace(&pos);

    const char* content = NULL;
    const char* type = NULL;

    if (strncmp(pos, "NOTE:", 5) == 0) {
        type = "note";
        content = pos + 5;
    } else if (strncmp(pos, "TIP:", 4) == 0) {
        type = "tip";
        content = pos + 4;
    } else if (strncmp(pos, "IMPORTANT:", 10) == 0) {
        type = "important";
        content = pos + 10;
    } else if (strncmp(pos, "WARNING:", 8) == 0) {
        type = "warning";
        content = pos + 8;
    } else if (strncmp(pos, "CAUTION:", 8) == 0) {
        type = "caution";
        content = pos + 8;
    }

    if (type) {
        add_attribute_to_element(parser, admonition, "class", type);

        // Skip whitespace after colon
        skip_whitespace(&content);

        if (*content) {
            Item inline_content = parse_asciidoc_inline(parser, content);
            if (inline_content.item != ITEM_UNDEFINED) {
                list_push((List*)admonition, inline_content);
                increment_element_content_length(admonition);
            }
        }
    }

    parser->current_line++;
    return (Item){.item = (uint64_t)admonition};
}

// Parse AsciiDoc table
static Item parse_asciidoc_table(MarkupParser* parser) {
    parser->current_line++; // Skip opening |===

    Element* table = create_element(parser, "table");
    if (!table) return (Item){.item = ITEM_ERROR};

    Element* tbody = create_element(parser, "tbody");
    if (!tbody) return (Item){.item = ITEM_ERROR};

    bool header_parsed = false;
    Element* thead = NULL;

    // Safety counter to prevent infinite loops
    int max_iterations = 1000;
    int iterations = 0;

    while (parser->current_line < parser->line_count && iterations < max_iterations) {
        iterations++;
        const char* line = parser->lines[parser->current_line];

        // Check for table end |=== (same pattern as start, so we need better logic)
        if (is_asciidoc_table_start(line)) {
            // This is the closing |=== delimiter
            parser->current_line++; // Skip closing |===
            break;
        }

        // Skip empty lines
        if (is_empty_line(line)) {
            parser->current_line++;
            continue;
        }

        // Parse table row (must start with |)
        if (line[0] == '|') {
            Element* row = create_element(parser, "tr");
            if (!row) {
                parser->current_line++;
                continue;
            }

            // Split line by |
            const char* ptr = line + 1; // Skip first |
            const char* cell_start = ptr;

            // Safety counter for cell parsing
            int cell_count = 0;
            const int max_cells = 200;

            while (*ptr && cell_count < max_cells) {
                if (*ptr == '|' || *(ptr + 1) == '\0') {
                    // End of cell
                    int cell_len = ptr - cell_start;
                    if (*(ptr + 1) == '\0' && *ptr != '|') {
                        cell_len++; // Include last character if not |
                    }

                    char* cell_text = (char*)malloc(cell_len + 1);
                    strncpy(cell_text, cell_start, cell_len);
                    cell_text[cell_len] = '\0';

                    char* trimmed_cell = trim_whitespace(cell_text);
                    free(cell_text);

                    // Create cell element
                    const char* cell_tag = (!header_parsed) ? "th" : "td";
                    Element* cell = create_element(parser, cell_tag);
                    if (cell && trimmed_cell && strlen(trimmed_cell) > 0) {
                        Item cell_content = parse_asciidoc_inline(parser, trimmed_cell);
                        if (cell_content.item != ITEM_UNDEFINED) {
                            list_push((List*)cell, cell_content);
                            increment_element_content_length(cell);
                        }
                        list_push((List*)row, (Item){.item = (uint64_t)cell});
                        increment_element_content_length(row);
                    }

                    if (trimmed_cell) free(trimmed_cell);
                    cell_count++;

                    if (*ptr == '|') {
                        ptr++;
                        cell_start = ptr;
                    } else {
                        break;
                    }
                } else {
                    ptr++;
                }
            }

            // Add row to appropriate section
            if (!header_parsed) {
                if (!thead) {
                    thead = create_element(parser, "thead");
                }
                if (thead) {
                    list_push((List*)thead, (Item){.item = (uint64_t)row});
                    increment_element_content_length(thead);
                }
                header_parsed = true;
            } else {
                list_push((List*)tbody, (Item){.item = (uint64_t)row});
                increment_element_content_length(tbody);
            }
        }

        parser->current_line++;
    }

    // Add safety check for infinite loop detection
    if (iterations >= max_iterations) {
        fprintf(stderr, "Warning: AsciiDoc table parsing exceeded maximum iterations, terminating to prevent infinite loop\n");
    }

    // Add sections to table
    if (thead && ((TypeElmt*)thead->type)->content_length > 0) {
        list_push((List*)table, (Item){.item = (uint64_t)thead});
        increment_element_content_length(table);
    }

    if (((TypeElmt*)tbody->type)->content_length > 0) {
        list_push((List*)table, (Item){.item = (uint64_t)tbody});
        increment_element_content_length(table);
    }

    return (Item){.item = (uint64_t)table};
}

// Parse AsciiDoc inline content with formatting
static Item parse_asciidoc_inline(MarkupParser* parser, const char* text) {
    if (!text || strlen(text) == 0) {
        return (Item){.item = ITEM_UNDEFINED};
    }

    // Safety check for overly long content to prevent DoS
    size_t text_len = strlen(text);
    if (text_len > 10000) {
        // For very long content, just return as plain text to avoid parsing issues
        return (Item){.item = s2it(create_string(parser, text))};
    }

    // Return simple text for now - inline formatting can be added later
    return (Item){.item = s2it(create_string(parser, text))};
}

// Parse AsciiDoc links (placeholder for more complex link parsing)
static Item parse_asciidoc_link(MarkupParser* parser, const char** text) {
    // This would handle more complex AsciiDoc link syntax like http://example.com[Link Text]
    // For now, basic URL detection is handled in parse_asciidoc_inline
    return (Item){.item = ITEM_UNDEFINED};
}
