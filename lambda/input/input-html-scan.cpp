/**
 * @file input-html-scan.cpp
 * @brief Low-level HTML scanning and tokenization helpers
 *
 * This module contains character-level scanning, whitespace handling,
 * entity decoding, and attribute parsing extracted from the main HTML parser.
 */

#include "input-html-scan.h"
#include "input-html-tokens.h"
#include <cstring>
#include <cctype>
#include <cstdio>

extern "C" {
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
}

extern "C" void skip_whitespace(const char **text);

// HTML entity table (80+ named entities)
static const struct {
    const char* name;
    const char* value;
} html_entities[] = {
    {"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"quot", "\""}, {"apos", "'"},
    {"nbsp", "\u00A0"}, {"iexcl", "¡"}, {"cent", "¢"}, {"pound", "£"}, {"curren", "¤"},
    {"yen", "¥"}, {"brvbar", "¦"}, {"sect", "§"}, {"uml", "¨"}, {"copy", "©"},
    {"ordf", "ª"}, {"laquo", "«"}, {"not", "¬"}, {"shy", "\u00AD"}, {"reg", "®"},
    {"macr", "¯"}, {"deg", "°"}, {"plusmn", "±"}, {"sup2", "²"}, {"sup3", "³"},
    {"acute", "´"}, {"micro", "µ"}, {"para", "¶"}, {"middot", "·"}, {"cedil", "¸"},
    {"sup1", "¹"}, {"ordm", "º"}, {"raquo", "»"}, {"frac14", "¼"}, {"frac12", "½"},
    {"frac34", "¾"}, {"iquest", "¿"}, {"Agrave", "À"}, {"Aacute", "Á"}, {"Acirc", "Â"},
    {"Atilde", "Ã"}, {"Auml", "Ä"}, {"Aring", "Å"}, {"AElig", "Æ"}, {"Ccedil", "Ç"},
    {"Egrave", "È"}, {"Eacute", "É"}, {"Ecirc", "Ê"}, {"Euml", "Ë"}, {"Igrave", "Ì"},
    {"Iacute", "Í"}, {"Icirc", "Î"}, {"Iuml", "Ï"}, {"ETH", "Ð"}, {"Ntilde", "Ñ"},
    {"Ograve", "Ò"}, {"Oacute", "Ó"}, {"Ocirc", "Ô"}, {"Otilde", "Õ"}, {"Ouml", "Ö"},
    {"times", "×"}, {"Oslash", "Ø"}, {"Ugrave", "Ù"}, {"Uacute", "Ú"}, {"Ucirc", "Û"},
    {"Uuml", "Ü"}, {"Yacute", "Ý"}, {"THORN", "Þ"}, {"szlig", "ß"}, {"agrave", "à"},
    {"aacute", "á"}, {"acirc", "â"}, {"atilde", "ã"}, {"auml", "ä"}, {"aring", "å"},
    {"aelig", "æ"}, {"ccedil", "ç"}, {"egrave", "è"}, {"eacute", "é"}, {"ecirc", "ê"},
    {"euml", "ë"}, {"igrave", "ì"}, {"iacute", "í"}, {"icirc", "î"}, {"iuml", "ï"},
    {"eth", "ð"}, {"ntilde", "ñ"}, {"ograve", "ò"}, {"oacute", "ó"}, {"ocirc", "ô"},
    {"otilde", "õ"}, {"ouml", "ö"}, {"divide", "÷"}, {"oslash", "ø"}, {"ugrave", "ù"},
    {"uacute", "ú"}, {"ucirc", "û"}, {"uuml", "ü"}, {"yacute", "ý"}, {"thorn", "þ"},
    {"yuml", "ÿ"},
    {NULL, NULL}
};

// Find HTML entity by name
static const char* find_html_entity(const char* name, size_t len) {
    for (int i = 0; html_entities[i].name; i++) {
        if (strlen(html_entities[i].name) == len &&
            strncmp(html_entities[i].name, name, len) == 0) {
            return html_entities[i].value;
        }
    }
    return NULL;
}

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
                    const char* entity_value = find_html_entity(entity_start, entity_len);

                    if (entity_value) {
                        stringbuf_append_str(sb, entity_value);
                        *html = entity_end + 1; // skip past the semicolon
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
