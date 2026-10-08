/**
 * block_paragraph.cpp - Paragraph block parser
 *
 * Handles parsing of paragraph elements, which are the default/fallback
 * block type when no other block type is detected.
 *
 * Paragraphs collect consecutive lines of text until a different block
 * type is encountered or a blank line is found.
 */
#include "block_common.hpp"
#include "../markup_highlight.hpp"
#include "../../../../lib/mem.h"
#include <cstdio>
#include <cstring>
#include "lib/arraylist.h"
#include "lib/str.h"

namespace lambda {
namespace markup {

// Forward declaration for inline parsing
extern Item parse_inline_spans(MarkupParser* parser, const char* text);

/**
 * parse_rst_literal_block - Parse RST indented literal block following ::
 *
 * Returns a code block element with the indented content
 */
static Item parse_rst_literal_block(MarkupParser* parser) {
    // Skip empty lines first
    while (parser->current_line < parser->line_count &&
           is_empty_line(parser->lines[parser->current_line])) {
        parser->current_line++;
    }

    if (parser->current_line >= parser->line_count) {
        return Item{.item = ITEM_UNDEFINED};
    }

    // Determine base indentation from first content line
    const char* first = parser->lines[parser->current_line];
    int base_indent = 0;
    while (first[base_indent] == ' ') base_indent++;

    if (base_indent < 1) {
        return Item{.item = ITEM_UNDEFINED}; // Not indented, no literal block
    }

    // Collect indented lines
    ArrayList* code_lines = arraylist_new(16);
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];

        // Empty line might continue the block
        if (is_empty_line(line)) {
            // Check if there are more indented lines after
            size_t peek = parser->current_line + 1;
            while (peek < (size_t)parser->line_count && is_empty_line(parser->lines[peek])) {
                peek++;
            }
            if (peek < (size_t)parser->line_count) {
                const char* next = parser->lines[peek];
                int next_indent = 0;
                while (next[next_indent] == ' ') next_indent++;
                if (next_indent >= base_indent) {
                    // Include empty line
                    arraylist_append(code_lines, (ArrayListValue)"");
                    parser->current_line++;
                    continue;
                }
            }
            break; // End of literal block
        }

        // Check indentation
        int indent = 0;
        while (line[indent] == ' ') indent++;
        if (indent < base_indent) {
            break; // Back to regular indentation, end of block
        }

        // Strip base indentation
        arraylist_append(code_lines, (ArrayListValue)(line + base_indent));
        parser->current_line++;
    }

    if (code_lines->length == 0) {
        arraylist_free(code_lines);
        return Item{.item = ITEM_UNDEFINED};
    }

    // Create code element
    Element* pre = create_element(parser, "pre");
    if (!pre) {
        return Item{.item = ITEM_ERROR};
    }

    Element* code = create_element(parser, "code");
    if (!code) {
        return Item{.item = ITEM_ERROR};
    }

    // Build code content
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);
    for (int i = 0; i < code_lines->length; i++) {
        stringbuf_append_all(sb, 2, i > 0 ? "\n" : "", (const char*)code_lines->data[i]);
    }
    arraylist_free(code_lines);

    // Add text node to code element
    String* text = parser->builder.createString(sb->str->chars, sb->length);
    Item text_item = {.item = s2it(text)};
    list_push((List*)code, text_item);

    // Add code to pre
    list_push((List*)pre, Item{.item = (uint64_t)code});

    return Item{.item = (uint64_t)pre};
}

/**
 * is_setext_underline - Check if line is a setext heading underline
 *
 * Returns: 1 for === (h1), 2 for --- (h2), 0 if not a setext underline
 */
static int is_setext_underline(const char* line) {
    if (!line) return 0;

    const char* pos = line;

    // Skip up to 3 leading spaces
    int leading_spaces = 0;
    while (*pos == ' ' && leading_spaces < 3) {
        leading_spaces++;
        pos++;
    }

    // 4+ leading spaces means not a setext underline
    if (*pos == ' ') return 0;

    // Must be = or -
    if (*pos != '=' && *pos != '-') return 0;

    char underline_char = *pos;
    int count = 0;

    // Count the underline characters
    while (*pos == underline_char) {
        count++;
        pos++;
    }

    // Must have at least 1 character
    if (count < 1) return 0;

    // Skip trailing whitespace
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }

    // Must end with newline or end of string
    if (*pos != '\0' && *pos != '\n' && *pos != '\r') return 0;

    return (underline_char == '=') ? 1 : 2;
}

/**
 * parse_paragraph - Parse a paragraph element
 *
 * Creates a <p> element containing parsed inline content.
 * Collects multiple lines if they continue the paragraph.
 *
 * CommonMark: Paragraphs preserve soft line breaks (newlines) between lines.
 * Lines with any indentation can continue a paragraph as long as they
 * don't match another block type (except indented code - that doesn't
 * interrupt paragraphs).
 */
Item parse_paragraph(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Man page .B and .I directives: create a paragraph with formatted content
    if (parser->config.format == Format::MAN) {
        const char* first_line = parser->lines[parser->current_line];

        // Handle .B (bold) directive - entire line is bold
        if (strncmp(first_line, ".B ", 3) == 0 || strncmp(first_line, ".B\t", 3) == 0) {
            Element* para = create_element(parser, "p");
            if (!para) {
                parser->current_line++;
                return Item{.item = ITEM_ERROR};
            }

            Element* strong = create_element(parser, "strong");
            if (strong) {
                const char* content = first_line + 3;
                content = str_skip_line_space(content);
                if (*content) {
                    // Parse the content for nested formatting
                    Item inner = parse_inline_spans(parser, content);
                    if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
                        list_push((List*)strong, inner);
                    }
                }
                list_push((List*)para, Item{.item = (uint64_t)strong});
            }
            parser->current_line++;
            return Item{.item = (uint64_t)para};
        }

        // Handle .I (italic) directive - entire line is italic
        if (strncmp(first_line, ".I ", 3) == 0 || strncmp(first_line, ".I\t", 3) == 0) {
            Element* para = create_element(parser, "p");
            if (!para) {
                parser->current_line++;
                return Item{.item = ITEM_ERROR};
            }

            Element* em = create_element(parser, "em");
            if (em) {
                const char* content = first_line + 3;
                content = str_skip_line_space(content);
                if (*content) {
                    // Parse the content for nested formatting
                    Item inner = parse_inline_spans(parser, content);
                    if (inner.item != ITEM_ERROR && inner.item != ITEM_UNDEFINED) {
                        list_push((List*)em, inner);
                    }
                }
                list_push((List*)para, Item{.item = (uint64_t)em});
            }
            parser->current_line++;
            return Item{.item = (uint64_t)para};
        }

        // Handle .PP, .P, .LP (paragraph break) - skip these and return next block
        if (strcmp(first_line, ".PP") == 0 || strcmp(first_line, ".P") == 0 ||
            strcmp(first_line, ".LP") == 0) {
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};  // Skip paragraph break directives
        }

        // Handle .RS (start indent) and .RE (end indent) - skip for now
        if (strncmp(first_line, ".RS", 3) == 0 || strncmp(first_line, ".RE", 3) == 0) {
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};
        }

        // Skip unknown man directives (lines starting with .)
        // but don't skip regular text lines
        if (first_line[0] == '.' && !str_is_space(first_line[1])) {
            // Known directives that we don't handle yet - just skip
            parser->current_line++;
            return Item{.item = ITEM_UNDEFINED};
        }
    }

    Element* para = create_element(parser, "p");
    if (!para) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Use StringBuf to build content from potentially multiple lines
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    // For the first line, always add it to the paragraph
    const char* first_line = parser->lines[parser->current_line];
    const char* text = first_line;
    skip_whitespace(&text);
    // highlight mode: the joined text maps back to its lines (markup_highlight.hpp)
    highlight_begin_inline(parser);
    highlight_add_segment(parser, 0, parser->current_line, (int)(text - first_line)); // INT_CAST_OK: indent within one line
    stringbuf_append_str(sb, text);
    parser->current_line++;

    // Track if we encounter a setext underline at the end
    int setext_level = 0;

    // Collect continuation lines. Inline math joins like any other inline:
    // only a blank line or an interrupting block (including a `$$` math
    // block) ends a paragraph, so a `$` in the text never splits one.
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // A man macro begins a new block even when it follows filled text
        // without an intervening blank line.
        if (parser->config.format == Format::MAN && current[0] == '.' &&
            current[1] && !str_is_space(current[1])) break;

        // Empty line ends paragraph
        if (is_empty_line(current)) {
            break;
        }

        if (parser->config.format == Format::MARKDOWN &&
            parser->config.flavor != Flavor::COMMONMARK && is_footnote_definition(current)) break;

        // Check if current line is a setext underline
        // BUT: lazy continuation lines should NOT be treated as setext underlines
        // (they were collected from outside the container and are just paragraph text)
        int underline_level = is_setext_underline(current);
        if (underline_level > 0) {
            // Check if this line is a lazy continuation
            bool is_lazy = false;
            if (parser->state.lazy_lines &&
                (size_t)parser->current_line < parser->state.lazy_lines_count) {
                is_lazy = parser->state.lazy_lines[parser->current_line];
            }

            if (!is_lazy) {
                // This is a setext heading - consume the underline and stop
                setext_level = underline_level;
                parser->current_line++;
                break;
            }
            // Lazy continuation - treat as regular paragraph line, fall through
        }

        // Check if next line starts a different block type
        // NOTE: Indented code blocks do NOT interrupt paragraphs in CommonMark
        BlockType next_type = detect_block_type(parser, current);

        // These block types interrupt paragraphs:
        // - Headers, lists, blockquotes, thematic breaks, fenced code, HTML blocks
        // Indented code blocks (4+ spaces) do NOT interrupt paragraphs
        // For HEADER: we need to check if it's an ATX header (starts with #)
        // Setext headers are handled by detecting the underline above
        if (next_type == BlockType::HEADER) {
            // RST underline headings start on the title line and must end the paragraph.
            if (parser->config.format == Format::RST) break;
            // Only ATX headers (starting with #) interrupt Markdown paragraphs
            const char* pos = current;
            skip_whitespace(&pos);
            if (*pos == '#') {
                break;  // ATX header interrupts
            }
            // Otherwise this line is detected as setext due to next line being underline
            // But we should include this line and check for underline on next iteration
        } else if (next_type == BlockType::LIST_ITEM) {
            // When parsing list item content, list items ALWAYS interrupt paragraphs
            // This allows nested lists to work properly
            if (parser->state.parsing_list_content) {
                break;  // Allow list items to interrupt within list content
            }

            // CommonMark rules for list items interrupting paragraphs:
            // - Empty list items (no content after marker) CANNOT interrupt
            // - Unordered list items (-, *, +) with content CAN interrupt
            // - Ordered list items starting with 1 (1., 1)) with content CAN interrupt
            // - Ordered list items NOT starting with 1 CANNOT interrupt
            FormatAdapter* adapter = parser->adapter();
            if (adapter) {
                ListItemInfo list_info = adapter->detectListItem(current);
                if (!list_info.valid) {
                    // Not a valid list item, continue collecting paragraph
                } else {
                    // Check if there's actual content after the marker
                    bool has_content = list_info.text_start && *list_info.text_start &&
                        *list_info.text_start != '\r' && *list_info.text_start != '\n';

                    if (!has_content) {
                        // Empty list item cannot interrupt paragraph
                    } else if (!list_info.is_ordered) {
                        // Unordered list with content CAN interrupt
                        break;
                    } else if (list_info.number == 1) {
                        // Ordered list starting with 1 and content CAN interrupt
                        break;
                    }
                    // Ordered list not starting with 1 cannot interrupt - continue
                }
            } else {
                // Fallback: don't interrupt (safer default)
            }
        } else if (next_type == BlockType::QUOTE ||
                   next_type == BlockType::DIVIDER ||
                   next_type == BlockType::DIRECTIVE ||
                   next_type == BlockType::TABLE ||
                   next_type == BlockType::MATH) {
            break;  // These block types interrupt paragraphs
        } else if (next_type == BlockType::CODE_BLOCK) {
            // Check if it's a fenced code block (``` or ~~~)
            const char* pos = current;
            skip_whitespace(&pos);
            if (*pos == '`' || *pos == '~') {
                break;  // Fenced code interrupts paragraphs
            }
            // Indented code block - doesn't interrupt, fall through
        } else if (next_type == BlockType::RAW_HTML) {
            // HTML block types 1-6 can interrupt paragraphs, type 7 cannot
            if (html_block_can_interrupt_paragraph(current)) {
                break;  // HTML block types 1-6 interrupt paragraphs
            }
            // Type 7 HTML blocks don't interrupt - fall through
        }

        const char* content = current;
        skip_whitespace(&content);
        highlight_add_segment(parser, (int64_t)sb->length + 1, parser->current_line,
                              (int)(content - current)); // INT_CAST_OK: indent within one line

        // CommonMark: Add newline between lines (soft line break), not space
        stringbuf_append_all(sb, 2, "\n", content);
        parser->current_line++;
    }

    // If we found a setext underline, convert to heading instead of paragraph
    if (setext_level > 0) {
        const char* tag = (setext_level == 1) ? "h1" : "h2";
        Element* heading = create_element(parser, tag);
        if (!heading) {
            return Item{.item = ITEM_ERROR};
        }

        // Trim trailing whitespace from heading content
        // (trailing tabs/spaces on the last line before the underline should be removed)
        size_t heading_len = sb->length;
        while (heading_len > 0 && (sb->str->chars[heading_len-1] == ' ' ||
                                   sb->str->chars[heading_len-1] == '\t')) {
            heading_len--;
        }

        // Parse inline content for heading with trimmed length
        String* text_content = parser->builder.createString(sb->str->chars, heading_len);
        Item content = parse_inline_spans(parser, text_content->chars);
        highlight_end_inline(parser);

        if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
            list_push((List*)heading, content);
        }

        return Item{.item = (uint64_t)heading};
    }

    // RST: Check if paragraph ends with :: for literal block introduction
    if (parser->config.format == Format::RST && sb->length >= 2) {
        // Check if ends with ::
        const char* end = sb->str->chars + sb->length;
        if (end[-1] == ':' && end[-2] == ':') {
            // Trim trailing whitespace before ::
            size_t len = sb->length - 2;
            while (len > 0 && (sb->str->chars[len-1] == ' ' || sb->str->chars[len-1] == '\t' ||
                               sb->str->chars[len-1] == '\n')) {
                len--;
            }

            // RST rules:
            // - "text::" -> "text:" (single colon at end)
            // - " ::" or just "::" -> paragraph is omitted
            bool omit_para = (len == 0);
            if (!omit_para) {
                // Keep single : at end
                sb->length = len + 1;
                sb->str->chars[len] = ':';
                sb->str->chars[len + 1] = '\0';
            }

            // Parse literal block
            Item literal = parse_rst_literal_block(parser);

            if (omit_para) {
                // Just return the literal block
                if (literal.item != ITEM_UNDEFINED && literal.item != ITEM_ERROR) {
                    return literal;
                }
                // No literal block either, return undefined
                return Item{.item = ITEM_UNDEFINED};
            }

            // Return paragraph, then literal block
            // Need to create a container or return para and queue literal
            // For now, create paragraph normally, then if literal exists,
            // we need to return both. The cleanest way is to wrap in a div.
            String* text_content = parser->builder.createString(sb->str->chars, sb->length);
            Item content = parse_inline_spans(parser, text_content->chars);

            if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
                list_push((List*)para, content);
            }

            if (literal.item != ITEM_UNDEFINED && literal.item != ITEM_ERROR) {
                // Wrap both in a div to return together
                Element* wrapper = create_element(parser, "div");
                if (wrapper) {
                    list_push((List*)wrapper, Item{.item = (uint64_t)para});
                    list_push((List*)wrapper, literal);
                    return Item{.item = (uint64_t)wrapper};
                }
            }

            return Item{.item = (uint64_t)para};
        }
    }

    // Parse inline content for paragraph
    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
    Item content = parse_inline_spans(parser, text_content->chars);
    highlight_end_inline(parser);

    if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED) {
        list_push((List*)para, content);
    }

    return Item{.item = (uint64_t)para};
}

/**
 * parse_rst_line_block - Parse RST line block (| prefix lines)
 *
 * RST line blocks preserve line structure with explicit line breaks.
 * Each line starting with | is a separate line in the output.
 */
Item parse_rst_line_block(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Create a div container for line block
    Element* div = create_element(parser, "div");
    if (!div) {
        parser->current_line++;
        return Item{.item = ITEM_ERROR};
    }

    // Set class attribute for styling
    String* class_key = parser->builder.createString("class");
    String* class_val = parser->builder.createString("line-block");
    if (class_key && class_val) {
        parser->builder.putToElement(lam::gc_borrow(div), class_key, Item{.item = s2it(class_val)});
    }

    // Collect all | prefix lines
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Skip leading whitespace
        const char* p = current;
        while (*p == ' ') p++;

        // Check for | prefix
        if (*p != '|') {
            break; // End of line block
        }

        // Get content after |
        p++; // Skip |
        if (*p == ' ') p++; // Skip optional space after |

        // Create a paragraph for each line
        Element* line_elem = create_element(parser, "p");
        if (line_elem) {
            // Parse inline content
            Item inline_content = parse_inline_spans(parser, p);
            if (inline_content.item != ITEM_ERROR && inline_content.item != ITEM_UNDEFINED) {
                list_push((List*)line_elem, inline_content);
            }

            list_push((List*)div, Item{.item = (uint64_t)line_elem});
        }

        parser->current_line++;
    }

    return Item{.item = (uint64_t)div};
}

/**
 * parse_rst_image_directive - Parse RST image directive
 *
 * Handles both .. image:: and .. figure:: directives
 * Creates an <img> element with src and optional attributes.
 */
Item parse_rst_image_directive(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Parse directive type and URL
    const char* p = line;
    while (*p == ' ') p++; // skip leading whitespace

    if (strncmp(p, ".. figure::", 11) == 0) {
        p += 11;
    } else if (strncmp(p, ".. image::", 10) == 0) {
        p += 10;
    } else {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }

    // Skip whitespace after ::
    while (*p == ' ') p++;

    // Get URL (rest of line)
    const char* url_start = p;
    while (*p && *p != '\n' && *p != '\r') p++;
    size_t url_len = p - url_start;

    // Trim trailing whitespace from URL
    while (url_len > 0 && (url_start[url_len-1] == ' ' ||
                          url_start[url_len-1] == '\n' ||
                          url_start[url_len-1] == '\r')) {
        url_len--;
    }

    parser->current_line++;

    // Create img element
    Element* img = create_element(parser, "img");
    if (!img) {
        return Item{.item = ITEM_ERROR};
    }

    // Set src attribute
    if (url_len > 0) {
        String* src_key = parser->builder.createString("src");
        String* src_val = parser->builder.createString(url_start, url_len);
        if (src_key && src_val) {
            parser->builder.putToElement(lam::gc_borrow(img), src_key, Item{.item = s2it(src_val)});
        }
    }

    // Parse option lines (indented lines starting with :name:)
    while (parser->current_line < parser->line_count) {
        const char* opt_line = parser->lines[parser->current_line];

        // Check for empty line - may or may not end options
        if (is_empty_line(opt_line)) {
            parser->current_line++;
            continue;
        }

        // Check for indentation (options must be indented)
        const char* op = opt_line;
        int indent = 0;
        while (*op == ' ') {
            indent++;
            op++;
        }

        // Not indented means end of directive options
        if (indent < 3) {
            break;
        }

        // Check for :option_name: format
        if (*op != ':') {
            break;
        }

        op++; // skip initial :
        const char* opt_name_start = op;
        while (*op && *op != ':' && *op != '\n') op++;
        if (*op != ':') {
            parser->current_line++;
            continue; // malformed option
        }

        size_t opt_name_len = op - opt_name_start;
        op++; // skip closing :

        // Skip whitespace after :name:
        while (*op == ' ') op++;

        // Get option value (rest of line)
        const char* opt_val_start = op;
        while (*op && *op != '\n' && *op != '\r') op++;
        size_t opt_val_len = op - opt_val_start;

        // Trim trailing whitespace
        while (opt_val_len > 0 && (opt_val_start[opt_val_len-1] == ' ')) {
            opt_val_len--;
        }

        // Map RST option names to HTML attribute names
        const char* attr_name = nullptr;
        char attr_buf[64];

        if (opt_name_len == 3 && strncmp(opt_name_start, "alt", 3) == 0) {
            attr_name = "alt";
        } else if (opt_name_len == 5 && strncmp(opt_name_start, "width", 5) == 0) {
            attr_name = "width";
        } else if (opt_name_len == 6 && strncmp(opt_name_start, "height", 6) == 0) {
            attr_name = "height";
        } else if (opt_name_len == 5 && strncmp(opt_name_start, "class", 5) == 0) {
            attr_name = "class";
        } else if (opt_name_len < 60) {
            // Use the option name as-is for other attributes
            str_copy(attr_buf, sizeof(attr_buf), opt_name_start, opt_name_len);
            attr_name = attr_buf;
        }

        if (attr_name && opt_val_len > 0) {
            String* key = parser->builder.createString(attr_name);
            String* val = parser->builder.createString(opt_val_start, opt_val_len);
            if (key && val) {
                parser->builder.putToElement(lam::gc_borrow(img), key, Item{.item = s2it(val)});
            }
        }

        parser->current_line++;
    }

    // For figure directive, we could wrap in <figure> but for now just return img
    return Item{.item = (uint64_t)img};
}

// Build the contents directive from headings already present in the source.
Item parse_rst_contents_directive(MarkupParser* parser, const char* line) {
    if (!parser || !line) return Item{.item = ITEM_ERROR};
    const char* marker = strstr(line, ".. contents::");
    if (!marker) return Item{.item = ITEM_ERROR};
    const char* title = marker + 13;
    title = str_skip_line_space(title);
    if (!*title) title = "Contents";

    parser->current_line++;
    int depth = 6;
    while (parser->current_line < parser->line_count) {
        const char* option = parser->lines[parser->current_line];
        if (*option != ' ' && *option != '\t') break;
        option = str_skip_line_space(option);
        if (*option != ':') break;
        if (strncmp(option, ":depth:", 7) == 0) {
            const char* value = str_skip_line_space(option + 7);
            int parsed_depth = 0;
            while (*value >= '0' && *value <= '9') {
                parsed_depth = parsed_depth * 10 + (*value - '0');
                value++;
            }
            if (parsed_depth > 0) depth = parsed_depth;
        }
        parser->current_line++;
    }

    Element* nav = create_element(parser, "nav");
    if (!nav) return Item{.item = ITEM_ERROR};
    add_attribute_to_element(parser, nav, "class", "rst-contents");
    Element* heading = create_element(parser, "p");
    if (heading) {
        add_attribute_to_element(parser, heading, "class", "rst-contents-title");
        String* title_text = parser->builder.createString(title);
        if (title_text) list_push((List*)heading, Item{.item = s2it(title_text)});
        list_push((List*)nav, Item{.item = (uint64_t)heading});
    }

    Element* list = create_element(parser, "ul");
    if (list) {
        for (int i = parser->current_line; i + 1 < parser->line_count; i++) {
            HeaderInfo info = parser->adapter()->detectHeader(
                parser->lines[i], parser->lines[i + 1]);
            if (!info.valid || rst_heading_level_at(parser, i) > depth ||
                !info.text_start || !info.text_end) continue;
            Element* item = create_element(parser, "li");
            Element* link = create_element(parser, "a");
            if (!item || !link) continue;
            char heading_id[48];
            char href[52];
            rst_heading_id(i, heading_id, sizeof(heading_id));
            snprintf(href, sizeof(href), "#%s", heading_id);
            add_attribute_to_element(parser, link, "href", href);
            String* text = parser->builder.createString(
                info.text_start, (size_t)(info.text_end - info.text_start));
            if (text) list_push((List*)link, Item{.item = s2it(text)});
            list_push((List*)item, Item{.item = (uint64_t)link});
            list_push((List*)list, Item{.item = (uint64_t)item});
        }
        list_push((List*)nav, Item{.item = (uint64_t)list});
    }
    return Item{.item = (uint64_t)nav};
}

// Parse a directive's indented body with the regular RST block parser. The
// temporary line view removes only the directive's common indentation.
static void parse_rst_directive_body(MarkupParser* parser, Element* parent,
                                     int first, int end, int base_indent) {
    int count = end - first;
    if (count <= 0) return;
    char** body_lines = (char**)mem_alloc(sizeof(char*) * count, MEM_CAT_INPUT_MARKUP);
    if (!body_lines) return;
    for (int i = 0; i < count; i++) {
        const char* source = parser->lines[first + i];
        int indent = 0;
        while (source[indent] == ' ' || source[indent] == '\t') indent++;
        body_lines[i] = (char*)(source + (indent < base_indent ? indent : base_indent));
    }

    char** saved_lines = parser->lines;
    int saved_count = parser->line_count;
    int saved_line = parser->current_line;
    ParserState saved_state = parser->state;
    parser->lines = body_lines;
    parser->line_count = count;
    parser->current_line = 0;
    parser->state.reset();
    while (parser->current_line < count) {
        int before = parser->current_line;
        Item child = parse_block_element(parser);
        if (child.item != ITEM_ERROR && child.item != ITEM_UNDEFINED) {
            list_push((List*)parent, child);
        }
        if (parser->current_line == before) parser->current_line++;
    }
    parser->lines = saved_lines;
    parser->line_count = saved_count;
    parser->current_line = saved_line;
    parser->state = saved_state;
    mem_free(body_lines);
}

static void rst_add_table_cell(MarkupParser* parser, Element* row,
                               const char* tag, const char* text, size_t length) {
    Element* cell = create_element(parser, tag);
    if (!cell) return;
    if (length) {
        String* value = parser->builder.createString(text, length);
        Item content = parse_table_cell_content(parser, value->chars);
        if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED)
            list_push((List*)cell, content);
    }
    list_push((List*)row, Item{.item = (uint64_t)cell});
}

static void rst_add_csv_row(MarkupParser* parser, Element* table,
                            const char* text, const char* tag) {
    Element* row = create_element(parser, "tr");
    if (!row) return;
    StrBuf* field = strbuf_new();
    bool quoted = false;
    for (const char* p = text;; p++) {
        if (*p == '"') {
            if (quoted && p[1] == '"') { strbuf_append_char(field, '"'); p++; }
            else quoted = !quoted;
        } else if ((*p == ',' && !quoted) || !*p) {
            rst_add_table_cell(parser, row, tag, field->str, field->length);
            strbuf_reset(field);
            if (!*p) break;
        } else {
            strbuf_append_char(field, *p);
        }
    }
    strbuf_free(field);
    list_push((List*)table, Item{.item = (uint64_t)row});
}

static void rst_add_table_caption(MarkupParser* parser, Element* table, const char* title) {
    if (!title || !*title) return;
    Element* caption = create_element(parser, "caption");
    if (!caption) return;
    String* content = parser->builder.createString(title);
    list_push((List*)caption, Item{.item = s2it(content)});
    list_push((List*)table, Item{.item = (uint64_t)caption});
}

static Item parse_rst_csv_table(MarkupParser* parser, int option_start,
                                int first, int end, const char* title) {
    Element* table = create_element(parser, "table");
    if (!table) return Item{.item = ITEM_ERROR};
    rst_add_table_caption(parser, table, title);
    for (int i = option_start; i < first; i++) {
        const char* option = str_skip_line_space(parser->lines[i]);
        if (strncmp(option, ":header:", 8) == 0)
            rst_add_csv_row(parser, table, str_skip_line_space(option + 8), "th");
    }
    for (int i = first; i < end; i++) {
        const char* source = str_skip_line_space(parser->lines[i]);
        if (!is_empty_line(source)) rst_add_csv_row(parser, table, source, "td");
    }
    return Item{.item = (uint64_t)table};
}

static void rst_finish_list_table_cell(MarkupParser* parser, Element* row,
                                       StrBuf* text, const char* tag) {
    if (!row) return;
    rst_add_table_cell(parser, row, tag, text->str, text->length);
    strbuf_reset(text);
}

static Item parse_rst_list_table(MarkupParser* parser, int option_start,
                                 int first, int end, int base_indent, const char* title) {
    Element* table = create_element(parser, "table");
    if (!table) return Item{.item = ITEM_ERROR};
    rst_add_table_caption(parser, table, title);
    int header_rows = 0;
    for (int i = option_start; i < first; i++) {
        const char* option = str_skip_line_space(parser->lines[i]);
        if (strncmp(option, ":header-rows:", 13) == 0)
            header_rows = (int)str_to_int64_default(option + 13, strlen(option + 13), 0);
    }
    Element* row = nullptr;
    StrBuf* cell_text = strbuf_new();
    int row_number = 0;
    bool has_cell = false;
    for (int i = first; i < end; i++) {
        const char* source = parser->lines[i];
        int indent = 0;
        while (source[indent] == ' ' || source[indent] == '\t') indent++;
        const char* content = source + (indent < base_indent ? indent : base_indent);
        if (strncmp(content, "* - ", 4) == 0) {
            if (has_cell) rst_finish_list_table_cell(parser, row, cell_text,
                                                      row_number <= header_rows ? "th" : "td");
            if (row) list_push((List*)table, Item{.item = (uint64_t)row});
            row = create_element(parser, "tr");
            row_number++;
            strbuf_append_str(cell_text, content + 4);
            has_cell = true;
        } else if (strncmp(content, "  - ", 4) == 0 && row) {
            if (has_cell) rst_finish_list_table_cell(parser, row, cell_text,
                                                      row_number <= header_rows ? "th" : "td");
            strbuf_append_str(cell_text, content + 4);
            has_cell = true;
        } else if (row && !is_empty_line(content)) {
            if (cell_text->length) strbuf_append_char(cell_text, '\n');
            strbuf_append_str(cell_text, str_skip_line_space(content));
        }
    }
    if (has_cell) rst_finish_list_table_cell(parser, row, cell_text,
                                              row_number <= header_rows ? "th" : "td");
    if (row) list_push((List*)table, Item{.item = (uint64_t)row});
    strbuf_free(cell_text);
    return Item{.item = (uint64_t)table};
}

Item parse_rst_directive(MarkupParser* parser, const char* line) {
    const char* marker = str_skip_line_space(line);
    const char* name_start = marker + 2;
    name_start = str_skip_line_space(name_start);
    const char* name_end = name_start;
    while (str_is_alnum(*name_end) || *name_end == '-' || *name_end == '_') name_end++;
    const char* separator = name_end[0] == ':' && name_end[1] == ':' ? name_end : nullptr;
    int directive_indent = (int)(marker - line);

    // RST comments have no directive separator and consume their indented body.
    if (!separator) {
        parser->current_line++;
        while (parser->current_line < parser->line_count) {
            const char* next = parser->lines[parser->current_line];
            if (!is_empty_line(next)) {
                int indent = 0;
                while (next[indent] == ' ' || next[indent] == '\t') indent++;
                if (indent <= directive_indent) break;
            }
            parser->current_line++;
        }
        return Item{.item = ITEM_UNDEFINED};
    }

    size_t name_len = (size_t)(separator - name_start);
    char name[48] = {};
    if (!name_len || name_len >= sizeof(name)) {
        parser->current_line++;
        return Item{.item = ITEM_UNDEFINED};
    }
    str_copy(name, sizeof(name), name_start, name_len);
    if (strcmp(name, "image") == 0 || strcmp(name, "figure") == 0)
        return parse_rst_image_directive(parser, line);
    if (strcmp(name, "contents") == 0)
        return parse_rst_contents_directive(parser, line);

    const char* argument = str_skip_line_space(separator + 2);
    size_t argument_len = strlen(argument);
    while (argument_len && (argument[argument_len - 1] == ' ' ||
                            argument[argument_len - 1] == '\t')) argument_len--;
    String* argument_text = parser->builder.createString(argument, argument_len);

    int first = parser->current_line + 1;
    int end = first;
    while (end < parser->line_count) {
        const char* next = parser->lines[end];
        if (!is_empty_line(next)) {
            int indent = 0;
            while (next[indent] == ' ' || next[indent] == '\t') indent++;
            if (indent <= directive_indent) break;
        }
        end++;
    }
    parser->current_line = end;

    // Field options precede the content and are not part of its block tree.
    while (first < end && is_empty_line(parser->lines[first])) first++;
    int option_start = first;
    while (first < end) {
        const char* option = str_skip_line_space(parser->lines[first]);
        if (*option != ':' || !strchr(option + 1, ':')) break;
        first++;
    }
    while (first < end && is_empty_line(parser->lines[first])) first++;
    int base_indent = 0;
    for (int i = first; i < end; i++) {
        const char* body_line = parser->lines[i];
        if (is_empty_line(body_line)) continue;
        int indent = 0;
        while (body_line[indent] == ' ' || body_line[indent] == '\t') indent++;
        if (!base_indent || indent < base_indent) base_indent = indent;
    }

    if (strcmp(name, "csv-table") == 0)
        return parse_rst_csv_table(parser, option_start, first, end, argument_text->chars);
    if (strcmp(name, "list-table") == 0)
        return parse_rst_list_table(parser, option_start, first, end, base_indent,
                                    argument_text->chars);

    if (strcmp(name, "code") == 0 || strcmp(name, "code-block") == 0 ||
        strcmp(name, "raw") == 0) {
        Element* pre = create_element(parser, "pre");
        Element* code = create_element(parser, "code");
        if (!pre || !code) return Item{.item = ITEM_ERROR};
        if (argument_len) add_attribute_to_element(parser, code, "language", argument_text->chars);
        StringBuf* sb = parser->sb;
        stringbuf_reset(sb);
        for (int i = first; i < end; i++) {
            if (i > first) stringbuf_append_char(sb, '\n');
            const char* body_line = parser->lines[i];
            int indent = 0;
            while (body_line[indent] == ' ' || body_line[indent] == '\t') indent++;
            stringbuf_append_str(sb, body_line + (indent < base_indent ? indent : base_indent));
        }
        String* content = parser->builder.createString(sb->str->chars, sb->length);
        list_push((List*)code, Item{.item = s2it(content)});
        list_push((List*)pre, Item{.item = (uint64_t)code});
        return Item{.item = (uint64_t)pre};
    }

    if (strcmp(name, "meta") == 0 || strcmp(name, "title") == 0 ||
        strcmp(name, "highlight") == 0 || strcmp(name, "default-role") == 0 ||
        strcmp(name, "role") == 0) return Item{.item = ITEM_UNDEFINED};

    Element* container = create_element(parser, "div");
    if (!container) return Item{.item = ITEM_ERROR};
    char class_name[80];
    snprintf(class_name, sizeof(class_name), "rst-directive rst-%s", name);
    if (strcmp(name, "container") == 0 && argument_len) {
        StrBuf* classes = strbuf_new();
        strbuf_append_all(classes, 2, class_name, " ");
        strbuf_append_str(classes, argument_text->chars);
        add_attribute_to_element(parser, container, "class", classes->str);
        strbuf_free(classes);
    } else {
        add_attribute_to_element(parser, container, "class", class_name);
    }
    if (argument_len && strcmp(name, "container") != 0) {
        Element* heading = create_element(parser, "p");
        if (heading) {
            add_attribute_to_element(parser, heading, "class", "rst-directive-title");
            list_push((List*)heading, Item{.item = s2it(argument_text)});
            list_push((List*)container, Item{.item = (uint64_t)heading});
        }
    }
    parse_rst_directive_body(parser, container, first, end, base_indent);
    return Item{.item = (uint64_t)container};
}

Item parse_man_tagged_paragraph(MarkupParser* parser, const char* line) {
    Element* list = create_element(parser, "dl");
    Element* term = create_element(parser, "dt");
    Element* definition = create_element(parser, "dd");
    if (!list || !term || !definition) return Item{.item = ITEM_ERROR};

    bool next_line_is_term = strncmp(line, ".TP", 3) == 0;
    const char* tag = nullptr;
    parser->current_line++;
    if (next_line_is_term) {
        if (parser->current_line < parser->line_count) {
            tag = parser->lines[parser->current_line++];
        }
    } else {
        tag = str_skip_line_space(line + 3);
    }
    if (tag && *tag) {
        bool bold = strncmp(tag, ".B ", 3) == 0;
        bool italic = strncmp(tag, ".I ", 3) == 0;
        if (bold || italic) tag = str_skip_line_space(tag + 3);
        if (*tag == '"') {
            tag++;
            const char* close = strchr(tag, '"');
            if (close) {
                String* trimmed = parser->builder.createString(tag, (size_t)(close - tag));
                tag = trimmed->chars;
            }
        }
        Item tag_content = parse_inline_spans(parser, tag);
        if (tag_content.item != ITEM_ERROR && tag_content.item != ITEM_UNDEFINED) {
            if (bold || italic) {
                Element* emphasis = create_element(parser, bold ? "strong" : "em");
                list_push((List*)emphasis, tag_content);
                tag_content = Item{.item = (uint64_t)emphasis};
            }
            list_push((List*)term, tag_content);
        }
    }
    if (parser->current_line < parser->line_count) {
        const char* body = parser->lines[parser->current_line];
        if (!is_empty_line(body) && body[0] != '.') {
            Item content = parse_paragraph(parser, body);
            if (content.item != ITEM_ERROR && content.item != ITEM_UNDEFINED)
                list_push((List*)definition, content);
        }
    }
    list_push((List*)list, Item{.item = (uint64_t)term});
    list_push((List*)list, Item{.item = (uint64_t)definition});
    return Item{.item = (uint64_t)list};
}

/**
 * parse_rst_definition_list - Parse RST definition list
 *
 * RST definition lists have the format:
 * Term
 *     Definition for term.
 *
 * Creates: <dl><dt>Term</dt><dd>Definition</dd></dl>
 */
Item parse_rst_definition_list(MarkupParser* parser, const char* line) {
    if (!parser || !line) {
        return Item{.item = ITEM_ERROR};
    }

    // Create the definition list container
    Element* dl = create_element(parser, "dl");
    if (!dl) {
        return Item{.item = ITEM_ERROR};
    }

    // Parse definition list entries
    while (parser->current_line < parser->line_count) {
        const char* current = parser->lines[parser->current_line];

        // Skip blank separators only while another term and definition follow.
        if (is_empty_line(current)) {
            parser->current_line++;
            if (!is_rst_definition_term(parser, parser->current_line)) break;
            continue;
        }

        // Stop before the next block instead of consuming it as an orphan term.
        if (!is_rst_definition_term(parser, parser->current_line)) break;

        // This is a term
        const char* term_start = current;
        const char* term_end = term_start;
        while (*term_end && *term_end != '\n' && *term_end != '\r') {
            term_end++;
        }
        size_t term_len = term_end - term_start;

        // Trim trailing whitespace from term
        while (term_len > 0 && (term_start[term_len-1] == ' ' || term_start[term_len-1] == '\t')) {
            term_len--;
        }

        // Create <dt> for term
        Element* dt = create_element(parser, "dt");
        if (dt) {
            Item term_inline = parse_inline_spans(parser, term_start);
            if (term_inline.item != ITEM_ERROR && term_inline.item != ITEM_UNDEFINED) {
                list_push((List*)dt, term_inline);
            }
            list_push((List*)dl, Item{.item = (uint64_t)dt});
        }

        parser->current_line++;

        // Check for definition (indented lines following the term)
        if (parser->current_line < parser->line_count) {
            const char* def_line = parser->lines[parser->current_line];

            // Definition must be indented
            if (*def_line == ' ' || *def_line == '\t') {
                // Collect all indented lines as the definition
                StringBuf* sb = parser->sb;
                stringbuf_reset(sb);

                while (parser->current_line < parser->line_count) {
                    const char* dl_line = parser->lines[parser->current_line];

                    if (is_empty_line(dl_line)) {
                        // Empty line - check if more definition content follows
                        size_t peek = parser->current_line + 1;
                        if (peek < (size_t)parser->line_count) {
                            const char* next = parser->lines[peek];
                            if (*next == ' ' || *next == '\t') {
                                // More indented content, include blank line
                                if (sb->length > 0) stringbuf_append_char(sb, '\n');
                                parser->current_line++;
                                continue;
                            }
                        }
                        break; // End of definition
                    }

                    // Check indentation
                    if (*dl_line != ' ' && *dl_line != '\t') {
                        break; // Back to term or end
                    }

                    // Strip leading whitespace
                    const char* dp = dl_line;
                    dp = str_skip_line_space(dp);

                    if (sb->length > 0) stringbuf_append_char(sb, ' ');
                    while (*dp && *dp != '\n' && *dp != '\r') {
                        stringbuf_append_char(sb, *dp);
                        dp++;
                    }

                    parser->current_line++;
                }

                // Create <dd> for definition
                Element* dd = create_element(parser, "dd");
                if (dd && sb->length > 0) {
                    Item def_inline = parse_inline_spans(parser, sb->str->chars);
                    if (def_inline.item != ITEM_ERROR && def_inline.item != ITEM_UNDEFINED) {
                        list_push((List*)dd, def_inline);
                    }
                    list_push((List*)dl, Item{.item = (uint64_t)dd});
                }
            }
        }
    }

    return Item{.item = (uint64_t)dl};
}

} // namespace markup
} // namespace lambda
