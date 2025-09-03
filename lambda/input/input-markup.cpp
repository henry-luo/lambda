#include "input.h"
#include "markup-parser.h"
#include <string.h>
#include <ctype.h>

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

// Phase 4: Advanced inline element parsers
static Item parse_strikethrough(MarkupParser* parser, const char** text);
static Item parse_superscript(MarkupParser* parser, const char** text);
static Item parse_subscript(MarkupParser* parser, const char** text);
static Item parse_emoji_shortcode(MarkupParser* parser, const char** text);
static Item parse_inline_math(MarkupParser* parser, const char** text);
static Item parse_small_caps(MarkupParser* parser, const char** text);

// Math parser integration functions
static Item parse_math_content(Input* input, const char* math_content, const char* flavor);
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

// Common utility functions
#define skip_whitespace input_skip_whitespace
#define is_whitespace_char input_is_whitespace_char
#define is_empty_line input_is_empty_line
#define count_leading_chars input_count_leading_chars
#define trim_whitespace input_trim_whitespace
#define split_lines input_split_lines
#define free_lines input_free_lines
#define create_element input_create_element
#define add_attribute_to_element input_add_attribute_to_element

// Parser lifecycle management
MarkupParser* parser_create(Input* input, ParseConfig config) {
    if (!input) return NULL;
    
    MarkupParser* parser = (MarkupParser*)calloc(1, sizeof(MarkupParser));
    if (!parser) return NULL;
    
    parser->input = input;
    parser->config = config;
    parser->lines = NULL;
    parser->line_count = 0;
    parser->current_line = 0;
    
    // Initialize state
    parser_reset_state(parser);
    
    return parser;
}

void parser_destroy(MarkupParser* parser) {
    if (!parser) return;
    
    if (parser->lines) {
        free_lines(parser->lines, parser->line_count);
    }
    
    free(parser);
}

void parser_reset_state(MarkupParser* parser) {
    if (!parser) return;
    
    // Reset parsing state
    memset(parser->state.list_markers, 0, sizeof(parser->state.list_markers));
    memset(parser->state.list_levels, 0, sizeof(parser->state.list_levels));
    parser->state.list_depth = 0;
    
    parser->state.table_state = 0;
    parser->state.in_code_block = false;
    parser->state.code_fence_char = 0;
    parser->state.code_fence_length = 0;
    
    parser->state.in_math_block = false;
    memset(parser->state.math_delimiter, 0, sizeof(parser->state.math_delimiter));
    
    // Phase 2: Reset additional state
    parser->state.header_level = 0;
    parser->state.in_quote_block = false;
    parser->state.quote_depth = 0;
    parser->state.in_table = false;
    parser->state.table_columns = 0;
}

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
            }
        }
    }
    
    // Content-based detection
    const char* line = content;
    size_t len = strlen(content);
    
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
            
        case MARKUP_RST:
        case MARKUP_TEXTILE:
        case MARKUP_ORG:
        default:
            return "standard";
    }
}

// Main parsing function
Item parse_markup_content(MarkupParser* parser, const char* content) {
    if (!parser || !content) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Split content into lines
    parser->lines = split_lines(content, &parser->line_count);
    if (!parser->lines) {
        return (Item){.item = ITEM_ERROR};
    }
    
    parser->current_line = 0;
    parser_reset_state(parser);
    
    // Parse the document
    return parse_document(parser);
}

// Document parsing - creates proper structure according to Doc Schema
static Item parse_document(MarkupParser* parser) {
    // Create root document element according to schema
    Element* doc = create_element(parser->input, "doc");
    if (!doc) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add version attribute as required by schema
    add_attribute_to_element(parser->input, doc, "version", "1.0");
    
    // Create meta element only if there's actual metadata content
    Element* meta = NULL;
    
    // Phase 6: Parse metadata first (YAML frontmatter, Org properties, or AsciiDoc defaults)
    if (has_yaml_frontmatter(parser)) {
        meta = create_element(parser->input, "meta");
        if (meta) {
            Item metadata = parse_yaml_frontmatter(parser);
            if (metadata.item != ITEM_UNDEFINED && metadata.item != ITEM_ERROR) {
                list_push((List*)meta, metadata);
                increment_element_content_length(meta);
            }
        }
    } else if (has_org_properties(parser)) {
        meta = create_element(parser->input, "meta");
        if (meta) {
            Item properties = parse_org_properties(parser);
            if (properties.item != ITEM_UNDEFINED && properties.item != ITEM_ERROR) {
                list_push((List*)meta, properties);
                increment_element_content_length(meta);
            }
        }
    } else if (parser->config.format == MARKUP_ASCIIDOC) {
        // For AsciiDoc: create default metadata like the old parser for compatibility
        meta = create_element(parser->input, "meta");
        if (meta) {
            // Add default title
            add_attribute_to_element(parser->input, meta, "title", "AsciiDoc Document");
            // Add default language
            add_attribute_to_element(parser->input, meta, "language", "en");
        }
    }
    
    // Add meta element to document only if it was created and has content
    if (meta) {
        list_push((List*)doc, (Item){.item = (uint64_t)meta});
        increment_element_content_length(doc);
    }
    
    // Create body element for document content
    Element* body = create_element(parser->input, "body");
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
    Element* header = create_element(parser->input, tag_name);
    if (!header) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add level attribute for compatibility
    char level_str[8];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(parser->input, header, "level", level_str);
    
    // Extract header text (handle both Markdown and RST styles)
    const char* text = line;
    if (*text == '#') {
        // Markdown-style header: skip # markers and whitespace
        while (*text == '#') text++;
        skip_whitespace(&text);
    } else if (parser->config.format == MARKUP_RST) {
        // RST-style underlined header: use the entire line as text
        // The underline will be skipped by incrementing current_line twice
        text = line;
        skip_whitespace(&text);
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
    
    return (Item){.item = (uint64_t)header};
}

// Parse paragraph with enhanced inline parsing - Creates HTML-like <p> element
static Item parse_paragraph(MarkupParser* parser, const char* line) {
    Element* para = create_element(parser->input, "p");
    if (!para) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Use StrBuf to build content from potentially multiple lines
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    // Collect paragraph lines (continue until empty line or different block type)
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        
        if (is_empty_line(current)) {
            break; // End of paragraph
        }
        
        BlockType next_type = detect_block_type(parser, current);
        if (next_type != BLOCK_PARAGRAPH) {
            break; // Next line is different block type
        }
        
        // Add current line to paragraph
        if (sb->length > 0) {
            strbuf_append_char(sb, ' '); // Add space between lines
        }
        
        const char* content = current;
        skip_whitespace(&content);
        strbuf_append_str(sb, content);
        
        parser->current_line++;
    }
    
    // Parse inline content with enhancements and add as children
    String* text_content = strbuf_to_string(sb);
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
    Element* content_container = create_element(parser->input, "div");
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
    Element* list = create_element(parser->input, is_ordered ? "ol" : "ul");
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
            Element* item = create_element(parser->input, "li");
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
    Element* code = create_element(parser->input, "code");
    if (!code) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, code, "type", "block");
    
    // Extract language from fence line (```python, ~~~javascript, etc.)
    const char* fence = line;
    skip_whitespace(&fence);
    if (*fence == '`' || *fence == '~') {
        fence += 3; // Skip fence chars
        skip_whitespace(&fence);
        if (*fence && !isspace(*fence)) {
            // Extract language
            char lang[32] = {0};
            int i = 0;
            while (*fence && !isspace(*fence) && i < 31) {
                lang[i++] = *fence++;
            }
            lang[i] = '\0';
            add_attribute_to_element(parser->input, code, "language", lang);
        }
    }
    
    parser->current_line++; // Skip opening fence
    
    // Collect code content until closing fence
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        
        // Check for closing fence
        if (is_code_fence(current)) {
            parser->current_line++; // Skip closing fence
            break;
        }
        
        // Add line to code content
        if (sb->length > 0) {
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, current);
        parser->current_line++;
    }
    
    // Create code content (no inline parsing for code blocks)
    String* code_content = strbuf_to_string(sb);
    Item text_item = {.item = s2it(code_content)};
    list_push((List*)code, text_item);
    increment_element_content_length(code);
    
    return (Item){.item = (uint64_t)code};
}

// Parse horizontal divider/rule
static Item parse_divider(MarkupParser* parser) {
    Element* hr = create_element(parser->input, "hr");
    if (!hr) {
        return (Item){.item = ITEM_ERROR};
    }
    
    return (Item){.item = (uint64_t)hr};
}

// Parse blockquote elements (> quoted text)
static Item parse_blockquote(MarkupParser* parser, const char* line) {
    Element* quote = create_element(parser->input, "blockquote");
    if (!quote) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Extract quote content (skip > and whitespace)
    const char* content = line;
    skip_whitespace(&content);
    if (*content == '>') {
        content++;
        skip_whitespace(&content);
    }
    
    // Parse inline content for quote text
    Item quote_content = parse_inline_spans(parser, content);
    if (quote_content.item != ITEM_ERROR && quote_content.item != ITEM_UNDEFINED) {
        list_push((List*)quote, quote_content);
        increment_element_content_length(quote);
    }
    
    parser->current_line++;
    return (Item){.item = (uint64_t)quote};
}

// Parse math blocks ($$...$$)
static Item parse_math_block(MarkupParser* parser, const char* line) {
    Element* math = create_element(parser->input, "math");
    if (!math) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, math, "type", "block");
    
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
                Item parsed_math = parse_math_content(parser->input, math_content, math_flavor);
                
                if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
                    list_push((List*)math, parsed_math);
                    increment_element_content_length(math);
                } else {
                    // Fallback to plain text if math parsing fails
                    String* content_str = create_string(parser->input->pool, math_content);
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
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];
        
        // Check for closing $$
        const char* pos = current;
        skip_whitespace(&pos);
        if (*pos == '$' && *(pos+1) == '$') {
            parser->current_line++; // Skip closing $$
            break;
        }
        
        // Add line to math content
        if (sb->length > 0) {
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, current);
        parser->current_line++;
    }
    
    // Parse the math content using the math parser
    String* math_content_str = strbuf_to_string(sb);
    const char* math_flavor = detect_math_flavor(math_content_str->chars);
    
    Item parsed_math = parse_math_content(parser->input, math_content_str->chars, math_flavor);
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
static Item parse_table_structure(MarkupParser* parser) {
    if (parser->current_line >= parser->line_count) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Create table element
    Element* table = create_element(parser->input, "table");
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
        add_attribute_to_element(parser->input, table, "align", alignment_spec);
        free(alignment_spec);
        alignment_spec = NULL;
    }
    
    // Parse header row if present
    if (has_header) {
        Element* thead = create_element(parser->input, "thead");
        if (thead) {
            // Parse the header row and convert cells to th elements
            const char* header_line = parser->lines[parser->current_line];
            Element* header_row = create_element(parser->input, "tr");
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
                    Element* th_cell = create_element(parser->input, "th");
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
    Element* tbody = create_element(parser->input, "tbody");
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
    while (*pos) {
        if (*pos == '|') {
            if (!found_dash) return false; // Must have at least one dash per column
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
    
    return found_dash; // Must end with valid column
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
    // Note: We need parser->input to add attributes, so we'll handle this in the calling function
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
        String* empty = input_create_string(parser->input, "");
        return (Item){.item = s2it(empty)};
    }
    
    // Trim whitespace
    while (*cell_text == ' ' || *cell_text == '\t') cell_text++;
    
    const char* end = cell_text + strlen(cell_text) - 1;
    while (end > cell_text && (*end == ' ' || *end == '\t')) end--;
    
    size_t len = end - cell_text + 1;
    char* trimmed = (char*)malloc(len + 1);
    if (!trimmed) {
        String* empty = input_create_string(parser->input, "");
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
    Element* row = create_element(parser->input, "tr");
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
        Element* cell = create_element(parser->input, "td");
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
    
    // For simple text without markup, return as string
    if (!strpbrk(text, "*_`[!~\\$:^{@'")) {
        String* content = input_create_string(parser->input, text);
        return (Item){.item = s2it(content)};
    }
    
    // Create span container for mixed inline content
    Element* span = create_element(parser->input, "span");
    if (!span) {
        String* content = input_create_string(parser->input, text);
        return (Item){.item = s2it(content)};
    }
    
    // Parse inline elements
    const char* pos = text;
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    while (*pos) {
        if (*pos == '*' || *pos == '_') {
            // Flush any accumulated text
            if (sb->length > 0) {
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
            }
            
            // Parse bold/italic
            Item inline_item = parse_bold_italic(parser, &pos);
            if (inline_item.item != ITEM_ERROR && inline_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, inline_item);
                increment_element_content_length(span);
            }
        }
        else if (*pos == '`') {
            // Flush text and parse code span
            if (sb->length > 0) {
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                String* key = input_create_string(parser->input, "content");
                elmt_put(span, key, text_item, parser->input->pool);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
            // Flush text and try to parse emoji shortcode
            if (sb->length > 0) {
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
            }
            
            const char* old_pos = pos;
            Item emoji_item = parse_emoji_shortcode(parser, &pos);
            if (emoji_item.item != ITEM_ERROR && emoji_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, emoji_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, add the colon to text buffer and advance
                strbuf_append_char(sb, ':');
                pos++;
            }
        }
        // Phase 6: Wiki template parsing {{template|args}}
        else if (*pos == '{' && *(pos+1) == '{') {
            // Flush text and parse wiki template
            if (sb->length > 0) {
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
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
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
            }
            
            const char* old_pos = pos;
            Item math_item = parse_inline_math(parser, &pos);
            if (math_item.item != ITEM_ERROR && math_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, math_item);
                increment_element_content_length(span);
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
            }
        }
        else {
            // Regular character, add to text buffer
            strbuf_append_char(sb, *pos);
            pos++;
        }
    }
    
    // Flush any remaining text
    if (sb->length > 0) {
        String* text_content = strbuf_to_string(sb);
        Item text_item = {.item = s2it(text_content)};
        list_push((List*)span, text_item);
        increment_element_content_length(span);
    }
    
    return (Item){.item = (uint64_t)span};
}

// Parse inline content with format-specific enhancements
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
                    Element* span = create_element(parser->input, "span");
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
                
                Element* span = create_element(parser->input, "span");
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

// Parse bold and italic text (**bold**, *italic*, __bold__, _italic_)
static Item parse_bold_italic(MarkupParser* parser, const char** text) {
    const char* start = *text;
    char marker = *start;  // * or _
    int count = 0;
    
    // Count consecutive markers
    while (*start == marker) {
        count++;
        start++;
    }
    
    if (count == 0) {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    // Find closing markers
    const char* pos = start;
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
            
            if (marker_count >= count) {
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
    
    // Create appropriate element
    Element* elem;
    if (count >= 2) {
        elem = create_element(parser->input, "strong");
    } else {
        elem = create_element(parser->input, "em");
    }
    
    if (!elem) {
        *text = end + end_count;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Extract content between markers
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, start, content_len);
        content[content_len] = '\0';
        
        // Recursively parse inline content
        Item inner_content = parse_inline_spans(parser, content);
        if (inner_content.item != ITEM_ERROR && inner_content.item != ITEM_UNDEFINED) {
            list_push((List*)elem, inner_content);
            increment_element_content_length(elem);
        }
        
        free(content);
    }
    
    *text = end + count;  // Move past closing markers
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
    
    Element* code = create_element(parser->input, "code");
    if (!code) {
        *text = end + backticks;
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, code, "type", "inline");
    
    // Extract and add code content (no further inline parsing)
    size_t content_len = end - start;
    char* content = (char*)malloc(content_len + 1);
    if (content) {
        strncpy(content, start, content_len);
        content[content_len] = '\0';
        
        String* code_text = input_create_string(parser->input, content);
        Item code_item = {.item = s2it(code_text)};
        list_push((List*)code, code_item);
        increment_element_content_length(code);
        
        free(content);
    }
    
    *text = end + backticks;
    return (Item){.item = (uint64_t)code};
}

// Parse links ([text](url))
static Item parse_link(MarkupParser* parser, const char** text) {
    const char* pos = *text;
    if (*pos != '[') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    pos++; // Skip [
    
    // Find closing ]
    const char* text_start = pos;
    const char* text_end = NULL;
    int bracket_depth = 1;
    
    while (*pos && bracket_depth > 0) {
        if (*pos == '[') bracket_depth++;
        else if (*pos == ']') bracket_depth--;
        
        if (bracket_depth == 0) {
            text_end = pos;
        }
        pos++;
    }
    
    if (!text_end || *pos != '(') {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    pos++; // Skip (
    
    // Find closing )
    const char* url_start = pos;
    const char* url_end = NULL;
    int paren_depth = 1;
    
    while (*pos && paren_depth > 0) {
        if (*pos == '(') paren_depth++;
        else if (*pos == ')') paren_depth--;
        
        if (paren_depth == 0) {
            url_end = pos;
        }
        pos++;
    }
    
    if (!url_end) {
        (*text)++;
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    Element* link = create_element(parser->input, "a");
    if (!link) {
        *text = pos;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add URL attribute
    size_t url_len = url_end - url_start;
    char* url = (char*)malloc(url_len + 1);
    if (url) {
        strncpy(url, url_start, url_len);
        url[url_len] = '\0';
        add_attribute_to_element(parser->input, link, "href", url);
        free(url);
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
    
    Element* img = create_element(parser->input, "img");
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
        add_attribute_to_element(parser->input, img, "src", src);
        free(src);
    }
    
    // Add alt attribute
    size_t alt_len = alt_end - alt_start;
    char* alt = (char*)malloc(alt_len + 1);
    if (alt) {
        strncpy(alt, alt_start, alt_len);
        alt[alt_len] = '\0';
        add_attribute_to_element(parser->input, img, "alt", alt);
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
    
    // Create parser
    MarkupParser* parser = parser_create(input, config);
    if (!parser) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Parse content
    Item result = parse_markup_content(parser, content);
    
    // Cleanup
    parser_destroy(parser);
    
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
    
    // Create parser
    MarkupParser* parser = parser_create(input, config);
    if (!parser) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Parse content
    Item result = parse_markup_content(parser, content);
    
    // Cleanup
    parser_destroy(parser);
    
    return result;
}

// Math parser integration functions
static Item parse_math_content(Input* input, const char* math_content, const char* flavor) {
    if (!input || !math_content) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Create a temporary Input to preserve the current state
    StrBuf* original_sb = input->sb;
    Item original_root = input->root;
    
    // Parse the math expression using the existing parse_math function
    // This modifies input->root, so we need to capture the result
    parse_math(input, math_content, flavor);
    Item result = input->root;
    
    // Restore original state
    input->root = original_root;
    input->sb = original_sb;
    
    return result;
}

static const char* detect_math_flavor(const char* content) {
    if (!content) return "latex";
    
    // Simple heuristics to detect math flavor
    // Look for LaTeX-specific commands
    if (strstr(content, "\\frac") || strstr(content, "\\sum") || 
        strstr(content, "\\int") || strstr(content, "\\alpha")) {
        return "latex";
    }
    
    // Look for Typst-specific syntax
    if (strstr(content, "frac(") || strstr(content, "sum_")) {
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
    
    // Simple check for pipe character (more sophisticated table detection possible)
    return (*pos == '|' || strchr(pos, '|') != NULL);
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
    Element* s_elem = create_element(parser->input, "s");
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
    String* content_str = input_create_string(parser->input, content);
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
    Element* sup_elem = create_element(parser->input, "sup");
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
    String* content_str = input_create_string(parser->input, content);
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
    Element* sub_elem = create_element(parser->input, "sub");
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
    String* content_str = input_create_string(parser->input, content);
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
    
    // Extract shortcode between :
    size_t shortcode_len = (pos + 1) - start; // Include both :
    char* shortcode = (char*)malloc(shortcode_len + 1);
    if (!shortcode) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(shortcode, start, shortcode_len);
    shortcode[shortcode_len] = '\0';
    
    // Look up emoji in table
    const char* emoji_char = NULL;
    for (int i = 0; emoji_map[i].shortcode; i++) {
        if (strcmp(shortcode, emoji_map[i].shortcode) == 0) {
            emoji_char = emoji_map[i].emoji;
            break;
        }
    }
    
    free(shortcode);
    
    if (!emoji_char) {
        // Unknown emoji shortcode
        return (Item){.item = ITEM_ERROR};
    }
    
    // Create emoji element
    Element* emoji_elem = create_element(parser->input, "emoji");
    if (!emoji_elem) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add emoji character as content
    String* emoji_str = input_create_string(parser->input, emoji_char);
    if (emoji_str) {
        Item emoji_item = {.item = s2it(emoji_str)};
        list_push((List*)emoji_elem, emoji_item);
        increment_element_content_length(emoji_elem);
    }
    
    *text = pos + 1; // Skip closing :
    
    return (Item){.item = (uint64_t)emoji_elem};
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
    Element* math_elem = create_element(parser->input, "math");
    if (!math_elem) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add type attribute for inline math
    add_attribute_to_element(parser->input, math_elem, "type", "inline");
    
    // Create content string
    char* content = (char*)malloc(content_len + 1);
    if (!content) {
        return (Item){.item = ITEM_ERROR};
    }
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';
    
    // Parse the math content using the math parser
    const char* math_flavor = detect_math_flavor(content);
    Item parsed_math = parse_math_content(parser->input, content, math_flavor);
    
    if (parsed_math.item != ITEM_ERROR && parsed_math.item != ITEM_UNDEFINED) {
        list_push((List*)math_elem, parsed_math);
        increment_element_content_length(math_elem);
    } else {
        // Fallback to plain text if math parsing fails
        String* math_str = input_create_string(parser->input, content);
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
    Element* footnote = create_element(parser->input, "footnote");
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
        add_attribute_to_element(parser->input, footnote, "id", id);
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
    
    Element* ref = create_element(parser->input, "footnote-ref");
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
        add_attribute_to_element(parser->input, ref, "ref", id);
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
    
    Element* citation = create_element(parser->input, "citation");
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
        add_attribute_to_element(parser->input, citation, "key", key);
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
                add_attribute_to_element(parser->input, citation, "info", info);
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
    Element* directive = create_element(parser->input, "directive");
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
        add_attribute_to_element(parser->input, directive, "type", name);
        
        // Add RST-specific attributes based on directive type
        if (strcmp(name, "code-block") == 0 || strcmp(name, "code") == 0) {
            add_attribute_to_element(parser->input, directive, "category", "code");
        } else if (strcmp(name, "note") == 0 || strcmp(name, "warning") == 0 || 
                   strcmp(name, "danger") == 0 || strcmp(name, "attention") == 0 ||
                   strcmp(name, "caution") == 0 || strcmp(name, "error") == 0 ||
                   strcmp(name, "hint") == 0 || strcmp(name, "important") == 0 ||
                   strcmp(name, "tip") == 0) {
            add_attribute_to_element(parser->input, directive, "category", "admonition");
        } else if (strcmp(name, "figure") == 0 || strcmp(name, "image") == 0) {
            add_attribute_to_element(parser->input, directive, "category", "media");
        } else if (strcmp(name, "toctree") == 0 || strcmp(name, "contents") == 0) {
            add_attribute_to_element(parser->input, directive, "category", "structure");
        } else {
            add_attribute_to_element(parser->input, directive, "category", "generic");
        }
        
        free(name);
    }
    
    // Skip :: and parse arguments
    if (*pos == ':' && *(pos+1) == ':') {
        pos += 2;
        skip_whitespace(&pos);
        
        if (*pos) {
            add_attribute_to_element(parser->input, directive, "args", pos);
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
                    
                    add_attribute_to_element(parser->input, directive, option_name, 
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
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];
        
        // Check if line is indented (part of directive) or empty
        if (is_empty_line(content_line)) {
            if (sb->length > 0) {
                strbuf_append_char(sb, '\n');
            }
            parser->current_line++;
        } else if (*content_line == ' ' || *content_line == '\t') {
            if (sb->length > 0) {
                strbuf_append_char(sb, '\n');
            }
            strbuf_append_str(sb, content_line);
            parser->current_line++;
        } else {
            break;
        }
    }
    
    // Add content if any
    if (sb->length > 0) {
        String* content_str = strbuf_to_string(sb);
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
    Element* org_block = create_element(parser->input, "org-block");
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
        add_attribute_to_element(parser->input, org_block, "type", type);
        free(type);
    }
    
    // Parse block arguments
    skip_whitespace(&pos);
    if (*pos) {
        add_attribute_to_element(parser->input, org_block, "args", pos);
    }
    
    parser->current_line++;
    
    // Build end marker
    char* end_marker = (char*)malloc(type_len + 10);
    sprintf(end_marker, "#+END_%.*s", (int)type_len, type_start);
    
    // Collect block content until end marker
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
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
            strbuf_append_char(sb, '\n');
        }
        strbuf_append_str(sb, content_line);
        parser->current_line++;
    }
    
    free(end_marker);
    
    // Add content
    if (sb->length > 0) {
        String* content_str = strbuf_to_string(sb);
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
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    const char* key_start = line;
    while (key_start < colon) {
        strbuf_append_char(sb, *key_start);
        key_start++;
    }
    
    // Trim key
    while (sb->length > 0 && (sb->str[sb->length-1] == ' ' || sb->str[sb->length-1] == '\t')) {
        sb->length--;
    }
    sb->str[sb->length] = '\0';
    
    if (sb->length == 0) return; // Empty key
    
    String* key = strbuf_to_string(sb);
    
    // Extract value
    const char* value_start = colon + 1;
    while (*value_start && (*value_start == ' ' || *value_start == '\t')) {
        value_start++;
    }
    
    strbuf_reset(sb);
    strbuf_append_str(sb, value_start);
    
    // Trim trailing whitespace from value
    while (sb->length > 0 && (sb->str[sb->length-1] == ' ' || sb->str[sb->length-1] == '\t' || 
                              sb->str[sb->length-1] == '\r' || sb->str[sb->length-1] == '\n')) {
        sb->length--;
    }
    sb->str[sb->length] = '\0';
    
    String* value = strbuf_to_string(sb);
    
    // Remove quotes if present
    if (value && value->len >= 2) {
        if ((value->chars[0] == '"' && value->chars[value->len-1] == '"') ||
            (value->chars[0] == '\'' && value->chars[value->len-1] == '\'')) {
            // Create unquoted version
            strbuf_reset(sb);
            strbuf_append_str_n(sb, value->chars + 1, value->len - 2);
            value = strbuf_to_string(sb);
        }
    }
    
    // Add as attribute to metadata element
    if (key && key->len > 0 && value && value->len > 0) {
        add_attribute_to_element(parser->input, metadata, key->chars, value->chars);
    }
}

// Parse YAML frontmatter (---)
static Item parse_yaml_frontmatter(MarkupParser* parser) {
    if (!has_yaml_frontmatter(parser)) {
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    Element* metadata = create_element(parser->input, "metadata");
    if (!metadata) {
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, metadata, "type", "yaml");
    
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
    
    Element* properties = create_element(parser->input, "metadata");
    if (!properties) {
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, properties, "type", "org");
    
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
                add_attribute_to_element(parser->input, properties, key, pos);
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
    Element* table = create_element(parser->input, "table");
    if (!table) return (Item){.item = ITEM_ERROR};
    
    parser->current_line++; // Skip {|
    
    Element* tbody = create_element(parser->input, "tbody");
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
            current_row = create_element(parser->input, "tr");
        } else if (is_wiki_table_row(line)) {
            // Table cell
            if (!current_row) {
                current_row = create_element(parser->input, "tr");
            }
            
            if (current_row) {
                // Parse cell content (skip leading |)
                const char* cell_content = trimmed + 1;
                while (*cell_content == ' ') cell_content++;
                
                Element* cell = create_element(parser->input, "td");
                if (cell) {
                    if (strlen(cell_content) > 0) {
                        // Wrap cell content in paragraph for proper block structure
                        Element* paragraph = create_element(parser->input, "p");
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
    
    Element* list = create_element(parser->input, list_tag);
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
        
        Element* list_item = create_element(parser->input, item_tag);
        if (!list_item) break;
        
        // Extract item content (skip markers and space)
        const char* content_start = line + item_level;
        if (*content_start == ' ') content_start++;
        
        char* content = trim_whitespace(content_start);
        if (content && strlen(content) > 0) {
            if (marker == '*' || marker == '#') {
                // Regular lists need paragraph wrapper
                Element* paragraph = create_element(parser->input, "p");
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
    
    Element* link_elem = create_element(parser->input, "a");
    if (!link_elem) return (Item){.item = ITEM_ERROR};
    
    // Extract link target
    int link_len = link_end - link_start;
    char* link_target = (char*)malloc(link_len + 1);
    strncpy(link_target, link_start, link_len);
    link_target[link_len] = '\0';
    add_attribute_to_element(parser->input, link_elem, "href", link_target);
    
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
        String* text_str = input_create_string(parser->input, display_text);
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
    
    Element* link_elem = create_element(parser->input, "a");
    if (!link_elem) return (Item){.item = ITEM_ERROR};
    
    // Extract URL
    int url_len = url_end - url_start;
    char* url = (char*)malloc(url_len + 1);
    strncpy(url, url_start, url_len);
    url[url_len] = '\0';
    add_attribute_to_element(parser->input, link_elem, "href", url);
    
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
        String* text_str = input_create_string(parser->input, display_text);
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
    
    Element* format_elem = create_element(parser->input, tag_name);
    if (!format_elem) return (Item){.item = ITEM_ERROR};
    
    // Extract content
    int content_len = content_end - content_start;
    if (content_len < 0) return (Item){.item = ITEM_ERROR};
    
    char* content = (char*)malloc(content_len + 1);
    if (!content) return (Item){.item = ITEM_ERROR};
    
    strncpy(content, content_start, content_len);
    content[content_len] = '\0';
    
    if (strlen(content) > 0) {
        String* text_str = input_create_string(parser->input, content);
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
    
    Element* template_elem = create_element(parser->input, "wiki-template");
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
            add_attribute_to_element(parser->input, template_elem, "name", content);
            add_attribute_to_element(parser->input, template_elem, "args", pipe_pos + 1);
        } else {
            add_attribute_to_element(parser->input, template_elem, "name", content);
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
    Element* hr = create_element(parser->input, "hr");
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
    Element* def_list = create_element(parser->input, "dl");
    if (!def_list) return (Item){.item = ITEM_ERROR};
    
    while (parser->current_line < parser->line_count && 
           is_rst_definition_list_item(parser->lines[parser->current_line])) {
        const char* term_line = parser->lines[parser->current_line];
        
        // create definition term
        Element* dt = create_element(parser->input, "dt");
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
            
            Element* dd = create_element(parser->input, "dd");
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
    Element* code_block = create_element(parser->input, "code");
    if (!code_block) return (Item){.item = ITEM_ERROR};
    
    parser->current_line++;
    
    // collect literal content
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    bool first_line = true;
    int base_indent = -1;
    
    while (parser->current_line < parser->line_count) {
        const char* content_line = parser->lines[parser->current_line];
        
        if (is_empty_line(content_line)) {
            if (!first_line) {
                strbuf_append_char(sb, '\n');
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
            strbuf_append_char(sb, '\n');
        }
        
        // add content with base indentation removed
        const char* content_start = content_line + base_indent;
        strbuf_append_str(sb, content_start);
        
        first_line = false;
        parser->current_line++;
    }
    
    // create string content
    if (sb->length > 0) {
        String* content_str = strbuf_to_string(sb);
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
    
    Element* comment = create_element(parser->input, "comment");
    if (!comment) return (Item){.item = ITEM_ERROR};
    
    const char* line = parser->lines[parser->current_line];
    const char* pos = line;
    skip_whitespace(&pos);
    pos += 2; // skip ..
    skip_whitespace(&pos);
    
    char* content = trim_whitespace(pos);
    if (content && strlen(content) > 0) {
        String* comment_text = input_create_string(parser->input, content);
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
    Element* table = create_element(parser->input, "table");
    if (!table) return (Item){.item = ITEM_ERROR};
    
    add_attribute_to_element(parser->input, table, "type", "grid");
    
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
    
    Element* code_elem = create_element(parser->input, "code");
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
        
        String* code_str = input_create_string(parser->input, content);
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
    
    Element* ref_elem = create_element(parser->input, "a");
    if (!ref_elem) {
        free(ref_text);
        (*text)++;
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, ref_elem, "href", ref_text);
    
    String* link_text = input_create_string(parser->input, ref_text);
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
        return (Item){.item = s2it(input_create_string(parser->input, ""))};
    }
    
    // Create a container element for mixed content
    Element* container = create_element(parser->input, "span");
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add bold element
                Element* bold = create_element(parser->input, "strong");
                char* bold_text = (char*)malloc(end - (ptr + 2) + 1);
                strncpy(bold_text, ptr + 2, end - (ptr + 2));
                bold_text[end - (ptr + 2)] = '\0';
                String* bold_str = input_create_string(parser->input, bold_text);
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add strong element
                Element* strong = create_element(parser->input, "strong");
                char* strong_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(strong_text, ptr + 1, end - (ptr + 1));
                strong_text[end - (ptr + 1)] = '\0';
                String* strong_str = input_create_string(parser->input, strong_text);
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add emphasis element
                Element* em = create_element(parser->input, "em");
                char* em_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(em_text, ptr + 1, end - (ptr + 1));
                em_text[end - (ptr + 1)] = '\0';
                String* em_str = input_create_string(parser->input, em_text);
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add code element
                Element* code = create_element(parser->input, "code");
                char* code_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(code_text, ptr + 1, end - (ptr + 1));
                code_text[end - (ptr + 1)] = '\0';
                String* code_str = input_create_string(parser->input, code_text);
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add superscript element
                Element* sup = create_element(parser->input, "sup");
                char* sup_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sup_text, ptr + 1, end - (ptr + 1));
                sup_text[end - (ptr + 1)] = '\0';
                String* sup_str = input_create_string(parser->input, sup_text);
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
                    String* before_str = input_create_string(parser->input, before);
                    list_push((List*)container, (Item){.item = s2it(before_str)});
                    increment_element_content_length(container);
                    free(before);
                }
                
                // Add subscript element
                Element* sub = create_element(parser->input, "sub");
                char* sub_text = (char*)malloc(end - (ptr + 1) + 1);
                strncpy(sub_text, ptr + 1, end - (ptr + 1));
                sub_text[end - (ptr + 1)] = '\0';
                String* sub_str = input_create_string(parser->input, sub_text);
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
        String* remaining_str = input_create_string(parser->input, remaining);
        list_push((List*)container, (Item){.item = s2it(remaining_str)});
        increment_element_content_length(container);
        free(remaining);
    }
    
    return (Item){.item = (uint64_t)container};
}

// Parse Textile code block (bc. or bc..)
static Item parse_textile_code_block(MarkupParser* parser, const char* line) {
    Element* code_block = create_element(parser->input, "pre");
    if (!code_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    bool extended = strncmp(line, "bc..", 4) == 0;
    add_attribute_to_element(parser->input, code_block, "extended", extended ? "true" : "false");
    
    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser->input, code_block, "modifiers", modifiers);
        free(modifiers);
    }
    
    const char* content = line + start_pos;
    String* code_content = input_create_string(parser->input, content);
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
            
            String* line_content = input_create_string(parser->input, next_line);
            list_push((List*)code_block, (Item){.item = s2it(line_content)});
            increment_element_content_length(code_block);
            parser->current_line++;
        }
    }
    
    return (Item){.item = (uint64_t)code_block};
}

// Parse Textile block quote (bq. or bq..)
static Item parse_textile_block_quote(MarkupParser* parser, const char* line) {
    Element* quote_block = create_element(parser->input, "blockquote");
    if (!quote_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    bool extended = strncmp(line, "bq..", 4) == 0;
    add_attribute_to_element(parser->input, quote_block, "extended", extended ? "true" : "false");
    
    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser->input, quote_block, "modifiers", modifiers);
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
    Element* pre_block = create_element(parser->input, "pre");
    if (!pre_block) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    // Parse modifiers and extract content
    int start_pos = 0;
    char* modifiers = parse_textile_modifiers(line, &start_pos);
    if (modifiers) {
        add_attribute_to_element(parser->input, pre_block, "modifiers", modifiers);
        free(modifiers);
    }
    
    const char* content = line + start_pos;
    String* pre_content = input_create_string(parser->input, content);
    list_push((List*)pre_block, (Item){.item = s2it(pre_content)});
    increment_element_content_length(pre_block);
    
    parser->current_line++;
    return (Item){.item = (uint64_t)pre_block};
}

// Parse Textile comment (###.)
static Item parse_textile_comment(MarkupParser* parser, const char* line) {
    Element* comment = create_element(parser->input, "!--");  // HTML comment style
    if (!comment) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    const char* content = line + 4; // Skip "###."
    String* comment_content = input_create_string(parser->input, content);
    list_push((List*)comment, (Item){.item = s2it(comment_content)});
    increment_element_content_length(comment);
    
    parser->current_line++;
    return (Item){.item = (uint64_t)comment};
}

// Parse Textile notextile block
static Item parse_textile_notextile(MarkupParser* parser, const char* line) {
    Element* notextile = create_element(parser->input, "notextile");
    if (!notextile) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    bool extended = strncmp(line, "notextile..", 11) == 0;
    const char* content = extended ? line + 11 : line + 10;
    while (*content && isspace(*content)) content++;
    
    String* raw_content = input_create_string(parser->input, content);
    list_push((List*)notextile, (Item){.item = s2it(raw_content)});
    increment_element_content_length(notextile);
    add_attribute_to_element(parser->input, notextile, "extended", extended ? "true" : "false");
    
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
    
    Element* list_item = create_element(parser->input, "li");
    if (!list_item) {
        parser->current_line++;
        return (Item){.item = ITEM_NULL};
    }
    
    const char* type_str = (list_type == '*') ? "bulleted" : 
                          (list_type == '#') ? "numbered" : 
                          (list_type == '-') ? "definition" : "unknown";
    add_attribute_to_element(parser->input, list_item, "type", type_str);
    
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
            String* term_str = input_create_string(parser->input, trimmed_term);
            
            Element* term_elem = create_element(parser->input, "dt");
            list_push((List*)term_elem, (Item){.item = s2it(term_str)});
            increment_element_content_length(term_elem);
            list_push((List*)list_item, (Item){.item = (uint64_t)term_elem});
            increment_element_content_length(list_item);
            
            free(term);
            free(trimmed_term);
            
            // Definition
            const char* definition = def_sep + 2;
            while (*definition && isspace(*definition)) definition++;
            
            Element* def_elem = create_element(parser->input, "dd");
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
    Element* header = create_element(parser->input, tag_name);
    if (!header) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    // Add level attribute (required by schema)
    char level_str[10];
    snprintf(level_str, sizeof(level_str), "%d", level);
    add_attribute_to_element(parser->input, header, "level", level_str);
    
    // Parse content after equals signs
    const char* pos = line;
    skip_whitespace(&pos);
    while (*pos == '=') pos++; // Skip equals signs
    skip_whitespace(&pos);
    
    if (*pos) {
        String* content = input_create_string(parser->input, pos);
        list_push((List*)header, (Item){.item = s2it(content)});
        increment_element_content_length(header);
    }
    
    parser->current_line++;
    return (Item){.item = (uint64_t)header};
}

// Parse AsciiDoc list
static Item parse_asciidoc_list(MarkupParser* parser) {
    Element* list = create_element(parser->input, "ul");
    if (!list) return (Item){.item = ITEM_ERROR};
    
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];
        
        if (!is_asciidoc_list_item(line)) {
            break;
        }
        
        // Create list item
        Element* list_item = create_element(parser->input, "li");
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
            Element* paragraph = create_element(parser->input, "p");
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
    Element* pre_block = create_element(parser->input, "pre");
    if (!pre_block) return (Item){.item = ITEM_ERROR};
    
    Element* code_block = create_element(parser->input, "code");
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
        
        String* content_str = input_create_string(parser->input, content);
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
    Element* admonition = create_element(parser->input, "div");
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
        add_attribute_to_element(parser->input, admonition, "class", type);
        
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
    
    Element* table = create_element(parser->input, "table");
    if (!table) return (Item){.item = ITEM_ERROR};
    
    Element* tbody = create_element(parser->input, "tbody");
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
            Element* row = create_element(parser->input, "tr");
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
                    Element* cell = create_element(parser->input, cell_tag);
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
                    thead = create_element(parser->input, "thead");
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
        return (Item){.item = s2it(input_create_string(parser->input, text))};
    }
    
    // Return simple text for now - inline formatting can be added later
    return (Item){.item = s2it(input_create_string(parser->input, text))};
}

// Parse AsciiDoc links (placeholder for more complex link parsing)
static Item parse_asciidoc_link(MarkupParser* parser, const char** text) {
    // This would handle more complex AsciiDoc link syntax like http://example.com[Link Text]
    // For now, basic URL detection is handled in parse_asciidoc_inline
    return (Item){.item = ITEM_UNDEFINED};
}
