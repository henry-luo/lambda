/**
 * @file input-html-scan.cpp
 * @brief Low-level HTML scanning and tokenization helpers
 *
 * This module contains character-level scanning, whitespace handling,
 * entity decoding, and attribute parsing extracted from the main HTML parser.
 *
 * Entity Handling Strategy:
 * - ASCII escapes (&lt; &gt; &amp; &quot; &apos;) are decoded inline to characters
 * - Numeric references (&#123; &#x1F;) are decoded inline to UTF-8
 * - Named entities (&copy; &mdash; etc.) are parsed as Lambda Symbol for roundtrip
 */

#include "input-html-scan.h"
#include "input-html-tokens.h"
#include "html_entities.h"
#include <cstring>
#include <cctype>
#include <cstdio>

extern "C" {
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
}

extern "C" void skip_whitespace(const char **text);

void html_to_lowercase(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

String* html_parse_string_content(StringBuf* sb, const char **html, char end_char) {
    int char_count = 0;
    const int MAX_CONTENT_CHARS = 10000000; // 10MB safety limit

    // handle empty string case - if we immediately encounter the end_char, just return empty string
    if (**html == end_char) {
        return stringbuf_to_string(sb);
    }

    while (**html && **html != end_char && char_count < MAX_CONTENT_CHARS) {
        if (**html == '&') {
            (*html)++; // skip &

            if (*html[0] == '#') {
                // numeric character reference
                (*html)++; // skip #
                int code = 0;
                bool hex = false;

                if (**html == 'x' || **html == 'X') {
                    hex = true;
                    (*html)++;
                }

                while (**html && **html != ';') {
                    if (hex) {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 16 + (**html - '0');
                        } else if (**html >= 'a' && **html <= 'f') {
                            code = code * 16 + (**html - 'a' + 10);
                        } else if (**html >= 'A' && **html <= 'F') {
                            code = code * 16 + (**html - 'A' + 10);
                        } else {
                            break;
                        }
                    } else {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 10 + (**html - '0');
                        } else {
                            break;
                        }
                    }
                    (*html)++;
                }

                if (**html == ';') {
                    (*html)++;
                    // convert Unicode code point to UTF-8
                    if (code < 0x80) {
                        stringbuf_append_char(sb, (char)code);
                    } else if (code < 0x800) {
                        stringbuf_append_char(sb, (char)(0xC0 | (code >> 6)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x10000) {
                        stringbuf_append_char(sb, (char)(0xE0 | (code >> 12)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else if (code < 0x110000) {
                        // 4-byte UTF-8 encoding for code points up to U+10FFFF
                        stringbuf_append_char(sb, (char)(0xF0 | (code >> 18)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 12) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | ((code >> 6) & 0x3F)));
                        stringbuf_append_char(sb, (char)(0x80 | (code & 0x3F)));
                    } else {
                        stringbuf_append_char(sb, '?'); // invalid code point
                    }
                } else {
                    stringbuf_append_char(sb, '&');
                    stringbuf_append_char(sb, '#');
                }
            } else {
                // named entity reference - use html_entities module
                const char *entity_start = *html;
                const char *entity_end = *html;

                // find the end of the entity name
                while (*entity_end && *entity_end != ';' && *entity_end != ' ' &&
                       *entity_end != '<' && *entity_end != '&') {
                    entity_end++;
                }

                if (*entity_end == ';') {
                    size_t entity_len = entity_end - entity_start;
                    EntityResult result = html_entity_resolve(entity_start, entity_len);

                    if (result.type == ENTITY_ASCII_ESCAPE) {
                        // ASCII escapes: decode inline
                        stringbuf_append_str(sb, result.decoded);
                        *html = entity_end + 1;
                    } else if (result.type == ENTITY_UNICODE_SPACE) {
                        // Unicode space entities: decode inline as UTF-8
                        char utf8_buf[8];
                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                        if (utf8_len > 0) {
                            stringbuf_append_str(sb, utf8_buf);
                        }
                        *html = entity_end + 1;
                    } else if (result.type == ENTITY_NAMED) {
                        // Named entities: for attribute values, still decode to UTF-8
                        // (Symbol handling is only for text content in elements)
                        char utf8_buf[8];
                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                        if (utf8_len > 0) {
                            stringbuf_append_str(sb, utf8_buf);
                        }
                        *html = entity_end + 1;
                    } else {
                        // unknown entity, preserve as-is for round-trip compatibility
                        stringbuf_append_char(sb, '&');
                        for (const char* p = entity_start; p < entity_end; p++) {
                            stringbuf_append_char(sb, *p);
                        }
                        stringbuf_append_char(sb, ';');
                        *html = entity_end + 1;
                    }
                } else {
                    // invalid entity format, just append the &
                    stringbuf_append_char(sb, '&');
                }
            }
        } else {
            stringbuf_append_char(sb, **html);
            (*html)++;
        }
        char_count++;
    }

    return stringbuf_to_string(sb);
}

String* html_parse_attribute_value(StringBuf* sb, const char **html, const char *html_start) {
    skip_whitespace(html);

    log_debug("Parsing attr value at char: %d, '%c'", (int)(*html - html_start), **html);
    if (**html == '"') {
        (*html)++; // skip opening quote

        stringbuf_reset(sb); // reset buffer before parsing quoted content
        String* value = html_parse_string_content(sb, html, '"');

        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '"') {
            (*html)++; // skip closing quote
        }

        // empty attributes ("") return NULL
        return value;
    } else if (**html == '\'') {
        (*html)++; // skip opening quote

        stringbuf_reset(sb); // reset buffer before parsing quoted content
        String* value = html_parse_string_content(sb, html, '\'');

        // CRITICAL FIX: Always skip the closing quote after parsing quoted content
        if (**html == '\'') {
            (*html)++; // skip closing quote
        }

        // empty attributes ('') return NULL
        return value;
    } else {
        // unquoted attribute value
        stringbuf_reset(sb); // reset buffer before parsing unquoted value
        int char_count = 0;
        const int MAX_CONTENT_CHARS = 10000000;

        while (**html && **html != ' ' && **html != '\t' && **html != '\n' &&
               **html != '\r' && **html != '>' && **html != '/' && **html != '=' &&
               char_count < MAX_CONTENT_CHARS) {
            stringbuf_append_char(sb, **html);
            (*html)++;
            char_count++;
        }

        if (char_count >= MAX_CONTENT_CHARS) {
            log_warn("Hit unquoted attribute value limit (%d)", MAX_CONTENT_CHARS);
        }

        return stringbuf_to_string(sb);
    }
}

String* html_parse_tag_name(StringBuf* sb, const char **html) {
    while (**html && **html != ' ' && **html != '\t' && **html != '\n' &&
           **html != '\r' && **html != '>' && **html != '/') {
        stringbuf_append_char(sb, tolower(**html));
        (*html)++;
    }
    return stringbuf_to_string(sb);
}

// C++ implementation of mixed content parsing with Symbol support
#ifdef __cplusplus
#include "../mark_builder.hpp"

/**
 * Helper to flush accumulated text as a String item
 */
static void flush_text_buffer(MarkBuilder& builder, StringBuf* sb,
                               HtmlMixedContentCallback callback, void* user_data) {
    if (sb->length > 0) {
        String* text_str = builder.createStringFromBuf(sb);
        if (text_str) {
            Item text_item = {.item = s2it(text_str)};
            callback(text_item, user_data);
        }
        stringbuf_reset(sb);
    }
}

void html_parse_mixed_content(
    MarkBuilder& builder,
    StringBuf* sb,
    const char **html,
    char end_char,
    HtmlMixedContentCallback callback,
    void* user_data
) {
    int char_count = 0;
    const int MAX_CONTENT_CHARS = 10000000; // 10MB safety limit

    stringbuf_reset(sb);

    // handle empty content case
    if (**html == end_char) {
        return;
    }

    while (**html && **html != end_char && char_count < MAX_CONTENT_CHARS) {
        if (**html == '&') {
            (*html)++; // skip &

            if (*html[0] == '#') {
                // numeric character reference - decode inline
                (*html)++; // skip #
                int code = 0;
                bool hex = false;

                if (**html == 'x' || **html == 'X') {
                    hex = true;
                    (*html)++;
                }

                while (**html && **html != ';') {
                    if (hex) {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 16 + (**html - '0');
                        } else if (**html >= 'a' && **html <= 'f') {
                            code = code * 16 + (**html - 'a' + 10);
                        } else if (**html >= 'A' && **html <= 'F') {
                            code = code * 16 + (**html - 'A' + 10);
                        } else {
                            break;
                        }
                    } else {
                        if (**html >= '0' && **html <= '9') {
                            code = code * 10 + (**html - '0');
                        } else {
                            break;
                        }
                    }
                    (*html)++;
                }

                if (**html == ';') {
                    (*html)++;
                    // convert Unicode code point to UTF-8
                    char utf8_buf[8];
                    int utf8_len = unicode_to_utf8((uint32_t)code, utf8_buf);
                    if (utf8_len > 0) {
                        stringbuf_append_str(sb, utf8_buf);
                    }
                } else {
                    stringbuf_append_char(sb, '&');
                    stringbuf_append_char(sb, '#');
                }
            } else {
                // named entity reference
                const char *entity_start = *html;
                const char *entity_end = *html;

                // find the end of the entity name
                while (*entity_end && *entity_end != ';' && *entity_end != ' ' &&
                       *entity_end != '<' && *entity_end != '&') {
                    entity_end++;
                }

                if (*entity_end == ';') {
                    size_t entity_len = entity_end - entity_start;
                    EntityResult result = html_entity_resolve(entity_start, entity_len);

                    if (result.type == ENTITY_ASCII_ESCAPE) {
                        // ASCII escapes: decode inline to text buffer
                        stringbuf_append_str(sb, result.decoded);
                        *html = entity_end + 1;
                    } else if (result.type == ENTITY_UNICODE_SPACE) {
                        // Unicode space entities: decode inline as UTF-8
                        char utf8_buf[8];
                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                        if (utf8_len > 0) {
                            stringbuf_append_str(sb, utf8_buf);
                        }
                        *html = entity_end + 1;
                    } else if (result.type == ENTITY_NAMED) {
                        // Named entities: flush text buffer, emit Symbol
                        flush_text_buffer(builder, sb, callback, user_data);

                        // Create symbol from entity name using builder
                        Item sym_item = builder.createSymbolItem(result.named.name);
                        callback(sym_item, user_data);

                        *html = entity_end + 1;
                    } else {
                        // unknown entity - preserve as-is in text buffer
                        stringbuf_append_char(sb, '&');
                        for (const char* p = entity_start; p < entity_end; p++) {
                            stringbuf_append_char(sb, *p);
                        }
                        stringbuf_append_char(sb, ';');
                        *html = entity_end + 1;
                    }
                } else {
                    // invalid entity format, just append the &
                    stringbuf_append_char(sb, '&');
                }
            }
        } else {
            stringbuf_append_char(sb, **html);
            (*html)++;
        }
        char_count++;
    }

    // Flush any remaining text
    flush_text_buffer(builder, sb, callback, user_data);
}

#endif // __cplusplus
