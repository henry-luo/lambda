/**
 * inline_spans.cpp - Main inline content parser
 *
 * This file implements the parse_inline_spans function that parses
 * inline content within text, detecting and creating elements for
 * emphasis, code, links, images, math, and other inline markup.
 *
 * Phase 3 of Markup Parser Refactoring:
 * Extracted from input-markup.cpp lines 1558-1907
 */
#include "inline_common.hpp"
#include "../../../lib/strbuf.h"
#include "../../../lib/log.h"

namespace lambda {
namespace markup {

// Helper: Create string from parser
static inline String* create_string(MarkupParser* parser, const char* text) {
    return parser->builder.createString(text);
}

// Helper: Create element from parser
static inline Element* create_element(MarkupParser* parser, const char* tag) {
    return parser->builder.element(tag).final().element;
}

// Helper: Increment element content length
static inline void increment_element_content_length(Element* elem) {
    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    elmt_type->content_length++;
}

/**
 * parse_inline_spans - Parse inline content with all inline elements
 *
 * This is the main entry point for inline parsing. It scans text for
 * inline markup and creates a span element containing parsed children.
 *
 * @param parser The markup parser
 * @param text The text to parse for inline elements
 * @return Item containing span element with parsed content
 */
Item parse_inline_spans(MarkupParser* parser, const char* text) {
    if (!parser || !text || !*text) {
        return Item{.item = ITEM_UNDEFINED};
    }

    log_debug("parse_inline_spans: input='%s', len=%zu", text, strlen(text));

    // For simple text without markup, return as string
    // Check for any potential inline markup characters
    // Also include newline since we need to check for hard line breaks (2+ spaces before \n)
    if (!strpbrk(text, "*_`[!~\\$:^{@'<&\n\r")) {
        log_debug("parse_inline_spans: no markup chars, returning as plain string");
        String* content = create_string(parser, text);
        return Item{.item = s2it(content)};
    }

    log_debug("parse_inline_spans: creating span, parsing inline content");

    // Create span container for mixed inline content
    Element* span = create_element(parser, "span");
    if (!span) {
        // Fallback to plain text
        String* content = create_string(parser, text);
        return Item{.item = s2it(content)};
    }

    // Make a local copy of the text since we use the shared parser->sb which
    // might be the source of the text pointer (e.g., when called from block_quote)
    size_t text_len = strlen(text);
    char* text_copy = (char*)malloc(text_len + 1);
    if (!text_copy) {
        String* content = create_string(parser, text);
        return Item{.item = s2it(content)};
    }
    memcpy(text_copy, text, text_len + 1);

    // Get string buffer from parser context
    StringBuf* sb = parser->sb;
    stringbuf_reset(sb);

    const char* pos = text_copy;
    Format format = parser->config.format;

    while (*pos) {
        // Check for emphasis markers (* or _)
        if (*pos == '*' || *pos == '_') {
            // Try to parse emphasis - don't flush buffer yet in case it fails
            const char* try_pos = pos;
            Item inline_item = parse_emphasis(parser, &try_pos, text_copy);

            if (inline_item.item != ITEM_ERROR && inline_item.item != ITEM_UNDEFINED) {
                // Success - flush buffer first, then add emphasis element
                if (sb->length > 0) {
                    String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                    Item text_item = {.item = s2it(text_content)};
                    list_push((List*)span, text_item);
                    increment_element_content_length(span);
                    stringbuf_reset(sb);
                }
                list_push((List*)span, inline_item);
                increment_element_content_length(span);
                pos = try_pos;  // Advance past the emphasis
            } else {
                // Emphasis parsing failed - treat entire marker run as plain text
                // This prevents second marker from being tried as opener
                char marker = *pos;
                while (*pos == marker) {
                    stringbuf_append_char(sb, *pos);
                    pos++;
                }
            }
            continue;
        }

        // Check for code span (`)
        if (*pos == '`') {
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
                continue;
            }
            // Code span failed - treat backtick as literal text
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for raw HTML (<) - Markdown only
        if (*pos == '<' && format == Format::MARKDOWN) {
            // Flush text first
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            // Try autolink first (<http://...> or <email@...>)
            Item autolink_item = parse_autolink(parser, &pos);
            if (autolink_item.item != ITEM_ERROR && autolink_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, autolink_item);
                increment_element_content_length(span);
                continue;
            }

            Item html_item = parse_raw_html(parser, &pos);
            if (html_item.item != ITEM_ERROR && html_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, html_item);
                increment_element_content_length(span);
                continue;
            }

            // Not valid HTML, add < to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for link or special bracket content ([)
        if (*pos == '[') {
            // Flush text first
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            // MediaWiki-specific link parsing
            if (format == Format::WIKI && *(pos+1) == '[') {
                Item wiki_link_item = parse_wiki_link(parser, &pos);
                if (wiki_link_item.item != ITEM_ERROR && wiki_link_item.item != ITEM_UNDEFINED) {
                    list_push((List*)span, wiki_link_item);
                    increment_element_content_length(span);
                    continue;
                }
            }

            // MediaWiki external link
            if (format == Format::WIKI) {
                Item wiki_external = parse_wiki_external_link(parser, &pos);
                if (wiki_external.item != ITEM_ERROR && wiki_external.item != ITEM_UNDEFINED) {
                    list_push((List*)span, wiki_external);
                    increment_element_content_length(span);
                    continue;
                }
            }

            // Check for footnote reference [^1]
            if (*(pos+1) == '^') {
                Item footnote_ref = parse_footnote_reference(parser, &pos);
                if (footnote_ref.item != ITEM_ERROR && footnote_ref.item != ITEM_UNDEFINED) {
                    list_push((List*)span, footnote_ref);
                    increment_element_content_length(span);
                    continue;
                }
            }

            // Check for citation [@key]
            if (*(pos+1) == '@') {
                Item citation = parse_citation(parser, &pos);
                if (citation.item != ITEM_ERROR && citation.item != ITEM_UNDEFINED) {
                    list_push((List*)span, citation);
                    increment_element_content_length(span);
                    continue;
                }
            }

            // Regular link parsing
            Item link_item = parse_link(parser, &pos);
            if (link_item.item != ITEM_ERROR && link_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, link_item);
                increment_element_content_length(span);
                continue;
            }

            // If not a link, add the [ character to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // MediaWiki bold/italic (')
        if (*pos == '\'' && format == Format::WIKI) {
            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            const char* old_pos = pos;
            Item wiki_format = parse_wiki_bold_italic(parser, &pos);
            if (wiki_format.item != ITEM_ERROR && wiki_format.item != ITEM_UNDEFINED) {
                list_push((List*)span, wiki_format);
                increment_element_content_length(span);
                continue;
            } else if (pos == old_pos) {
                // Parse failed and didn't advance, add character and move on
                stringbuf_append_char(sb, *pos);
                pos++;
                continue;
            }
        }

        // Check for image (![)
        if (*pos == '!' && *(pos+1) == '[') {
            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item image_item = parse_image(parser, &pos);
            if (image_item.item != ITEM_ERROR && image_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, image_item);
                increment_element_content_length(span);
                continue;
            }

            // Not an image, add the ! to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for strikethrough (~~)
        if (*pos == '~' && *(pos+1) == '~') {
            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item strike_item = parse_strikethrough(parser, &pos);
            if (strike_item.item != ITEM_ERROR && strike_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, strike_item);
                increment_element_content_length(span);
                continue;
            }

            // Not strikethrough, add ~ to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for superscript (^)
        if (*pos == '^') {
            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item sup_item = parse_superscript(parser, &pos);
            if (sup_item.item != ITEM_ERROR && sup_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, sup_item);
                increment_element_content_length(span);
                continue;
            }

            // Not superscript, add ^ to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for inline math ($)
        if (*pos == '$') {
            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item math_item = parse_inline_math(parser, &pos);
            if (math_item.item != ITEM_ERROR && math_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, math_item);
                increment_element_content_length(span);
                continue;
            }

            // Not math, add $ to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for emoji shortcode (:)
        if (*pos == ':') {
            const char* old_pos = pos;

            // Flush text
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item emoji_item = parse_emoji_shortcode(parser, &pos);
            if (emoji_item.item != ITEM_ERROR && emoji_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, emoji_item);
                increment_element_content_length(span);
                continue;
            }

            // Not emoji, restore position and add : to buffer
            pos = old_pos;
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for wiki template ({{)
        if (*pos == '{' && *(pos+1) == '{' && format == Format::WIKI) {
            // Flush text
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
                continue;
            }

            // Not template, add { to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Handle escape sequences (CommonMark §2.4)
        if (*pos == '\\') {
            char next = *(pos+1);
            log_debug("escape: found backslash, next char='%c' (0x%02x)", next, (unsigned char)next);

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
                    list_push((List*)span, Item{.item = (uint64_t)br});
                    increment_element_content_length(span);
                }

                pos += 2;
                // Skip optional \r after \n or vice versa (CRLF handling)
                if ((next == '\r' && *pos == '\n') || (next == '\n' && *pos == '\r')) {
                    pos++;
                }
                continue;
            }

            // Escapable punctuation: add the character literally without backslash
            if (next && is_escapable(next)) {
                log_debug("escape: handling escapable char '%c'", next);
                pos++; // Skip backslash
                stringbuf_append_char(sb, *pos);
                pos++;
                continue;
            }

            // Not an escape sequence: treat backslash as literal
            log_debug("escape: not escapable, keeping backslash");
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for entity reference (&)
        if (*pos == '&') {
            // Flush text first
            if (sb->length > 0) {
                String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                Item text_item = {.item = s2it(text_content)};
                list_push((List*)span, text_item);
                increment_element_content_length(span);
                stringbuf_reset(sb);
            }

            Item entity_item = parse_entity_reference(parser, &pos);
            if (entity_item.item != ITEM_ERROR && entity_item.item != ITEM_UNDEFINED) {
                list_push((List*)span, entity_item);
                increment_element_content_length(span);
                continue;
            }

            // Not a valid entity, add & to buffer
            stringbuf_append_char(sb, *pos);
            pos++;
            continue;
        }

        // Check for hard line break: 2+ spaces followed by newline
        if (*pos == ' ') {
            // Count trailing spaces
            const char* space_start = pos;
            while (*pos == ' ') pos++;
            int space_count = (int)(pos - space_start);

            // Check if followed by newline
            if (*pos == '\n' || *pos == '\r') {
                if (space_count >= 2) {
                    // Hard line break - trim trailing spaces from buffer and add <br>
                    // First strip any trailing spaces already in buffer
                    while (sb->length > 0 && sb->str->chars[sb->length - 1] == ' ') {
                        sb->length--;
                    }

                    // Flush text
                    if (sb->length > 0) {
                        String* text_content = parser->builder.createString(sb->str->chars, sb->length);
                        Item text_item = {.item = s2it(text_content)};
                        list_push((List*)span, text_item);
                        increment_element_content_length(span);
                        stringbuf_reset(sb);
                    }

                    // Create <br> element
                    Element* br = create_element(parser, "br");
                    if (br) {
                        list_push((List*)span, Item{.item = (uint64_t)br});
                        increment_element_content_length(span);
                    }

                    // Skip the newline
                    if (*pos == '\r' && *(pos+1) == '\n') pos += 2;
                    else pos++;
                    continue;
                }
            }

            // Not a hard break - add all the spaces to buffer
            for (int i = 0; i < space_count; i++) {
                stringbuf_append_char(sb, ' ');
            }
            continue;
        }

        // Regular character - add to buffer
        stringbuf_append_char(sb, *pos);
        pos++;
    }

    // Flush any remaining text
    if (sb->length > 0) {
        String* text_content = parser->builder.createString(sb->str->chars, sb->length);
        Item text_item = {.item = s2it(text_content)};
        list_push((List*)span, text_item);
        increment_element_content_length(span);
        stringbuf_reset(sb);  // Reset for any subsequent/parent calls
    }

    free(text_copy);  // Free the local copy we made
    return Item{.item = (uint64_t)span};
}

} // namespace markup
} // namespace lambda
