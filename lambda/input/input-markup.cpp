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

// Phase 2: Utility functions
static BlockType detect_block_type(const char* line);
static int get_header_level(const char* line);
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
            }
        }
    }
    
    // Content-based detection
    const char* line = content;
    size_t len = strlen(content);
    
    // Check for Org-mode patterns
    if (strstr(content, "#+TITLE:") || strstr(content, "#+AUTHOR:") || 
        strstr(content, "#+BEGIN_SRC") || strstr(content, "* ")) {
        return MARKUP_ORG;
    }
    
    // Check for reStructuredText patterns
    if (strstr(content, ".. ") || strstr(content, "===") || 
        strstr(content, "---") || strstr(content, "~~~")) {
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

// Document parsing - creates proper structure for markdown formatter
static Item parse_document(MarkupParser* parser) {
    // Create document structure that the markdown formatter can handle
    // Instead of a 'div' wrapper, create a 'body' container (which the formatter recognizes)
    Element* doc = create_element(parser->input, "body");
    if (!doc) {
        return (Item){.item = ITEM_ERROR};
    }
    
    // Parse content directly into the document
    while (parser->current_line < parser->line_count) {
        Item block = parse_block_element(parser);
        if (block.item != ITEM_UNDEFINED && block.item != ITEM_ERROR) {
            // Add block elements as children in the List structure
            list_push((List*)doc, block);
            // Increment content length for proper element tracking
            increment_element_content_length(doc);
        }
    }
    
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
    
    // Detect block type
    BlockType block_type = detect_block_type(line);
    
    switch (block_type) {
        case BLOCK_HEADER:
            return parse_header(parser, line);
        case BLOCK_LIST_ITEM:
            return parse_list_item(parser, line);
        case BLOCK_CODE_BLOCK:
            return parse_code_block(parser, line);
        case BLOCK_QUOTE:
            return parse_blockquote(parser, line);
        case BLOCK_TABLE:
            return parse_table_row(parser, line);
        case BLOCK_MATH:
            return parse_math_block(parser, line);
        case BLOCK_DIVIDER:
            parser->current_line++;
            return parse_divider(parser);
        case BLOCK_PARAGRAPH:
        default:
            return parse_paragraph(parser, line);
    }
}

// Parse header elements (# Header, ## Header, etc.) - Creates HTML-like h1, h2, etc.
static Item parse_header(MarkupParser* parser, const char* line) {
    int level = get_header_level(line);
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
    
    // Extract header text (skip # markers and whitespace)
    const char* text = line;
    while (*text == '#') text++;
    skip_whitespace(&text);
    
    // Parse inline content for header text and add as children
    Item content = parse_inline_spans(parser, text);
    if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
        list_push((List*)header, content);
        increment_element_content_length(header);
    }
    
    parser->current_line++;
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
        
        BlockType next_type = detect_block_type(current);
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
            BlockType block_type = detect_block_type(line);
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

// Parse table rows (|col1|col2|col3|)
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
        
        // Create table cell
        Element* cell = create_element(parser->input, "td");
        if (cell) {
            // Extract cell content
            size_t cell_len = cell_end - cell_start;
            char* cell_text = (char*)malloc(cell_len + 1);
            if (cell_text) {
                strncpy(cell_text, cell_start, cell_len);
                cell_text[cell_len] = '\0';
                
                // Trim whitespace from cell content
                char* trimmed = (char*)cell_text;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                char* end = trimmed + strlen(trimmed) - 1;
                while (end > trimmed && (*end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                
                // Parse cell content
                Item cell_content = parse_inline_spans(parser, trimmed);
                if (cell_content.item != ITEM_ERROR && cell_content.item != ITEM_UNDEFINED) {
                    list_push((List*)cell, cell_content);
                    increment_element_content_length(cell);
                }
                
                free(cell_text);
            }
            
            // Add cell to row
            list_push((List*)row, (Item){.item = (uint64_t)cell});
            increment_element_content_length(row);
        }
        
        // Move to next cell
        pos = cell_end;
        if (*pos == '|') pos++;
        
        if (!*pos) break;
    }
    
    parser->current_line++;
    return (Item){.item = (uint64_t)row};
}

// Parse math blocks ($$...$$)
static Item parse_math_block(MarkupParser* parser, const char* line) {
    Element* math = create_element(parser->input, "math");
    if (!math) {
        parser->current_line++;
        return (Item){.item = ITEM_ERROR};
    }
    
    add_attribute_to_element(parser->input, math, "type", "block");
    
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
    
    // Create math content (no inline parsing for math blocks)
    String* math_content = strbuf_to_string(sb);
    Item text_item = {.item = s2it(math_content)};
    list_push((List*)math, text_item);
    increment_element_content_length(math);
    
    return (Item){.item = (uint64_t)math};
}

// Phase 2: Enhanced inline content parsing with spans
static Item parse_inline_spans(MarkupParser* parser, const char* text) {
    if (!text || !*text) {
        return (Item){.item = ITEM_UNDEFINED};
    }
    
    // For simple text without markup, return as string
    if (!strpbrk(text, "*_`[!~\\$:^")) {
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
            // Flush text and parse link
            if (sb->length > 0) {
                String* text_content = strbuf_to_string(sb);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                strbuf_reset(sb);
            }
            
            Item link_item = parse_link(parser, &pos);
            if (link_item.item != ITEM_ERROR && link_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, link_item);
                increment_element_content_length(span);
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
                // Parse failed and didn't advance, advance manually to avoid infinite loop
                pos++;
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
    
    // Detect format
    MarkupFormat format = detect_markup_format(content, NULL);
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

// Phase 2: Utility functions for enhanced parsing

// Detect the type of a block element
static BlockType detect_block_type(const char* line) {
    if (!line || !*line) return BLOCK_PARAGRAPH;
    
    const char* pos = line;
    skip_whitespace(&pos);
    
    // Header detection (# ## ### etc.)
    if (*pos == '#') {
        int count = 0;
        while (*pos == '#' && count < 6) {
            count++;
            pos++;
        }
        if (count > 0 && (*pos == ' ' || *pos == '\t' || *pos == '\0')) {
            return BLOCK_HEADER;
        }
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

// Get header level (1-6)
static int get_header_level(const char* line) {
    if (!line) return 0;
    
    const char* pos = line;
    skip_whitespace(&pos);
    
    int level = 0;
    while (*pos == '#' && level < 6) {
        level++;
        pos++;
    }
    
    // Must be followed by space or end of line
    if (level > 0 && (*pos == ' ' || *pos == '\t' || *pos == '\0')) {
        return level;
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

// Emoji shortcode mapping table (subset of GitHub emojis)
static const struct {
    const char* shortcode;
    const char* emoji;
} emoji_map[] = {
    // Common emotions
    {":smile:", "😄"}, {":grinning:", "😀"}, {":laughing:", "😆"}, {":joy:", "😂"},
    {":wink:", "😉"}, {":blush:", "😊"}, {":heart_eyes:", "😍"}, {":kissing_heart:", "😘"},
    {":worried:", "😟"}, {":frowning:", "☹️"}, {":cry:", "😢"}, {":sob:", "😭"},
    
    // Common symbols
    {":heart:", "❤️"}, {":star:", "⭐"}, {":fire:", "🔥"}, {":zap:", "⚡"},
    {":100:", "💯"}, {":heavy_check_mark:", "✔️"}, {":x:", "❌"}, {":exclamation:", "❗"},
    
    // GitHub specific
    {":octocat:", "🐙"}, {":shipit:", "🚀"}, {":bowtie:", "👔"},
    
    // Programming/Tech
    {":computer:", "💻"}, {":bug:", "🐛"}, {":gear:", "⚙️"}, {":wrench:", "🔧"},
    {":hammer:", "🔨"}, {":electric_plug:", "🔌"}, {":bulb:", "💡"}, {":lock:", "🔒"},
    {":key:", "🔑"}, {":mag:", "🔍"},
    
    // Gestures
    {":thumbsup:", "👍"}, {":thumbsdown:", "👎"}, {":clap:", "👏"}, {":wave:", "👋"},
    {":point_right:", "👉"}, {":point_left:", "👈"}, {":point_up:", "👆"}, {":point_down:", "👇"},
    
    // Objects  
    {":phone:", "📱"}, {":camera:", "📷"}, {":book:", "📖"}, {":pencil:", "✏️"},
    {":memo:", "📝"}, {":email:", "✉️"}, {":mailbox:", "📮"}, {":inbox_tray:", "📥"},
    
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
    
    // Add math content as string
    String* math_str = input_create_string(parser->input, content);
    if (math_str) {
        Item math_item = {.item = s2it(math_str)};
        list_push((List*)math_elem, math_item);
        increment_element_content_length(math_elem);
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
