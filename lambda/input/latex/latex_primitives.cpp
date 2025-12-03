// latex_primitives.cpp - Character-level parsing (PEG.js style)
// Implements primitive rules: char, digit, ligature, charsym, diacritic, etc.

#include "latex_parser.hpp"
#include "latex_registry.hpp"
#include "../../../lib/log.h"
#include <cstring>

namespace lambda {
namespace latex {

// =============================================================================
// Primitive Dispatch
// =============================================================================

Item LatexParser::parse_primitive() {
    // PEG.js style primitive rule - try each primitive in order
    if (Item r = parse_ligature(); r.item != ITEM_NULL) return r;
    if (Item r = parse_ctrl_space(); r.item != ITEM_NULL) return r;
    if (Item r = parse_nbsp(); r.item != ITEM_NULL) return r;
    if (Item r = parse_utf8_char(); r.item != ITEM_NULL) return r;
    return ItemNull;
}

// =============================================================================
// Character Primitives
// =============================================================================

Item LatexParser::parse_char() {
    // char: [a-zA-Z] (catcode 11 characters)
    if (isalpha(peek())) {
        char c = advance();
        return create_text(&c, 1);
    }
    return ItemNull;
}

Item LatexParser::parse_digit() {
    // digit: [0-9] (catcode 12)
    if (isdigit(peek())) {
        char c = advance();
        return create_text(&c, 1);
    }
    return ItemNull;
}

Item LatexParser::parse_punctuation() {
    // punctuation: [.,;:*/()!?=+<>] (catcode 12)
    if (strchr(".,;:*/()!?=+<>", peek())) {
        char c = advance();
        return create_text(&c, 1);
    }
    return ItemNull;
}

// =============================================================================
// Space Handling
// =============================================================================

Item LatexParser::parse_space() {
    // significant space that becomes output
    // don't produce space at paragraph breaks
    if (is_paragraph_break()) return ItemNull;

    if (peek() == ' ' || peek() == '\t' || peek() == '\n') {
        advance();
        // collapse multiple spaces/newlines
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\n')) {
            if (peek() == '\n') {
                // check for paragraph break
                if (is_paragraph_break()) break;
            }
            advance();
        }
        return create_space();
    }
    return ItemNull;
}

Item LatexParser::parse_ctrl_space() {
    // PEG.js control space: \<space>, \<newline>, or \ followed by space
    if (pos_ + 1 < end_ && pos_[0] == '\\') {
        if (pos_[1] == ' ' || pos_[1] == '\n' || pos_[1] == '\t') {
            advance(); advance();
            // return breakable space (ZWSP + space in PEG.js)
            return create_text("\u200B ", 4);  // ZWSP + space
        }
    }
    return ItemNull;
}

Item LatexParser::parse_nbsp() {
    // ~ produces non-breaking space
    if (match('~')) {
        return create_nbsp();  // U+00A0
    }
    return ItemNull;
}

// =============================================================================
// Ligatures (PEG.js style)
// =============================================================================

Item LatexParser::parse_ligature() {
    // ligatures: ---, --, ``, '', <<, >>, etc.
    // Check longer ligatures first

    // em dash (---)
    if (match("---")) {
        return create_text("—", 3);  // U+2014 em dash
    }

    // en dash (--)
    if (match("--")) {
        return create_text("–", 3);  // U+2013 en dash
    }

    // double quotes
    if (match("``")) {
        return create_text("\u201C", 3);  // U+201C left double quote
    }
    if (match("''")) {
        return create_text("\u201D", 3);  // U+201D right double quote
    }

    // guillemets
    if (match("<<")) {
        return create_text("«", 2);  // U+00AB left guillemet
    }
    if (match(">>")) {
        return create_text("»", 2);  // U+00BB right guillemet
    }

    // inverted punctuation (Spanish)
    if (match("!`")) {
        return create_text("¡", 2);  // U+00A1 inverted exclamation
    }
    if (match("?`")) {
        return create_text("¿", 2);  // U+00BF inverted question
    }

    return ItemNull;
}

Item LatexParser::parse_hyphen() {
    // -, --, --- dash handling
    if (peek() != '-') return ItemNull;

    if (match("---")) {
        return create_text("—", 3);  // em dash
    }
    if (match("--")) {
        return create_text("–", 3);  // en dash
    }
    if (match("-")) {
        return create_text("-", 1);  // regular hyphen
    }

    return ItemNull;
}

Item LatexParser::parse_quotes() {
    // `' quote handling with smart quote conversion

    // check for double quotes first
    if (match("``")) {
        return create_text("\u201C", 3);
    }
    if (match("''")) {
        return create_text("\u201D", 3);
    }

    // single quotes
    if (match("`")) {
        return create_text("\u2018", 3);  // U+2018 left single quote
    }
    if (match("'")) {
        return create_text("\u2019", 3);  // U+2019 right single quote
    }

    return ItemNull;
}

// =============================================================================
// Control Symbols
// =============================================================================

Item LatexParser::parse_ctrl_sym() {
    // \$, \%, \#, \&, \{, \}, \_, etc.
    if (peek() != '\\') return ItemNull;

    if (pos_ + 1 >= end_) return ItemNull;
    char next = pos_[1];

    // check for control symbols
    if (strchr("$%#&{}_ ", next)) {
        advance();  // skip backslash
        char c = advance();

        // special handling for control space
        if (c == ' ') {
            return create_text("\u200B ", 4);  // ZWSP + space
        }

        return create_text(&c, 1);
    }

    // special control symbols
    if (next == ',') {
        advance(); advance();
        // \, = thin space (1/6 em)
        return create_element("thinspace");
    }
    if (next == '-') {
        advance(); advance();
        // \- = soft hyphen
        return create_text("\u00AD", 2);
    }
    if (next == '/') {
        advance(); advance();
        // \/ = italic correction (ZWNJ for word boundary)
        return create_text("\u200C", 3);
    }
    if (next == '@') {
        advance(); advance();
        // \@ = end-of-sentence marker (zero-width space)
        return create_text("\u200B", 3);
    }

    return ItemNull;
}

// =============================================================================
// Character Code Notation (PEG.js \char, ^^XX, ^^^^XXXX)
// =============================================================================

Item LatexParser::parse_charsym() {
    // \symbol{num}, \char, ^^XX, ^^^^XXXX

    // \symbol{num}
    if (lookahead("\\symbol")) {
        match("\\symbol");
        expect('{');
        int code = parse_integer();
        expect('}');
        return char_from_code(code);
    }

    // \char with various formats
    if (lookahead("\\char")) {
        match("\\char");

        // \char'77 (octal)
        if (match('\'')) {
            int code = parse_octal();
            return char_from_code(code);
        }
        // \char"FF (hex)
        if (match('"')) {
            int code = parse_hex(2);
            return char_from_code(code);
        }
        // \char98 (decimal)
        int code = parse_integer();
        return char_from_code(code);
    }

    // ^^^^XXXX (4-digit hex)
    if (match("^^^^")) {
        int code = parse_hex(4);
        return char_from_code(code);
    }

    // ^^XX (2-digit hex or character manipulation)
    if (match("^^")) {
        // check for 2-digit hex
        if (!at_end() && isxdigit(peek()) &&
            remaining() > 1 && isxdigit(pos_[1])) {
            int code = parse_hex(2);
            return char_from_code(code);
        }

        // single char: if code < 64, add 64; else subtract 64
        if (!at_end()) {
            char c = advance();
            int code = (unsigned char)c;
            code = (code < 64) ? code + 64 : code - 64;
            return char_from_code(code);
        }
    }

    return ItemNull;
}

// =============================================================================
// Diacritics
// =============================================================================

Item LatexParser::parse_diacritic() {
    // \', \`, \^, \", \~, \=, \., \u, \v, \H, \c, \d, \b, \r, \k, \t
    if (peek() != '\\') return ItemNull;
    if (pos_ + 1 >= end_) return ItemNull;

    char cmd = pos_[1];
    const DiacriticInfo* diac = find_diacritic(cmd);
    if (!diac) return ItemNull;

    // found a diacritic command
    advance();  // skip backslash
    advance();  // skip command char

    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    bool has_base = false;

    // check for braced argument
    if (peek() == '{') {
        advance();  // skip {

        if (peek() != '}') {
            // get the base character
            if (peek() == '\\') {
                // special: \i (dotless i) or \j (dotless j)
                advance();  // skip backslash
                if (peek() == 'i') {
                    stringbuf_append_str(sb, "\u0131");  // dotless i
                    has_base = true;
                    advance();
                } else if (peek() == 'j') {
                    stringbuf_append_str(sb, "\u0237");  // dotless j
                    has_base = true;
                    advance();
                } else {
                    // other escaped char
                    stringbuf_append_char(sb, peek());
                    has_base = true;
                    advance();
                }
            } else {
                // regular character - handle UTF-8
                append_utf8_char(sb);
                has_base = true;
            }
        }

        // skip to closing brace
        while (!at_end() && peek() != '}') advance();
        if (peek() == '}') advance();

    } else if (peek() == '\\' && (pos_[1] == 'i' || pos_[1] == 'j')) {
        // \"\i or \"\j style
        advance();  // skip backslash
        if (peek() == 'i') {
            stringbuf_append_str(sb, "\u0131");
        } else {
            stringbuf_append_str(sb, "\u0237");
        }
        has_base = true;
        advance();
        // command gobbles trailing space
        if (peek() == ' ') advance();

    } else if (peek() && peek() != ' ' && peek() != '\n' && peek() != '\t' &&
               peek() != '\\' && peek() != '{' && peek() != '}') {
        // unbraced single character: \^o
        append_utf8_char(sb);
        has_base = true;
    }

    if (has_base) {
        // append combining diacritic after base character
        stringbuf_append_str(sb, diac->combining);
    } else {
        // no base - use standalone form
        stringbuf_append_str(sb, diac->standalone);
        stringbuf_append_str(sb, "\u200B");  // ZWSP for word boundary
    }

    return create_text(sb->str->chars, sb->length);
}

// Helper to append a UTF-8 character to StringBuf and advance pos_
void LatexParser::append_utf8_char(StringBuf* sb) {
    unsigned char c = (unsigned char)peek();

    if ((c & 0x80) == 0) {
        // ASCII
        stringbuf_append_char(sb, advance());
    } else if ((c & 0xE0) == 0xC0) {
        // 2-byte UTF-8
        stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
    } else if ((c & 0xF0) == 0xE0) {
        // 3-byte UTF-8
        stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
    } else if ((c & 0xF8) == 0xF0) {
        // 4-byte UTF-8
        stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
        if (!at_end()) stringbuf_append_char(sb, advance());
    }
}

// =============================================================================
// UTF-8 Character Parsing
// =============================================================================

Item LatexParser::parse_utf8_char() {
    // any non-special UTF-8 character
    // skip special characters
    if (strchr(" \t\n\r\\{}$&#^_%~[]", peek())) {
        return ItemNull;
    }

    unsigned char c = (unsigned char)peek();

    if ((c & 0x80) == 0) {
        // ASCII
        char ch = advance();
        return create_text(&ch, 1);
    }

    // multi-byte UTF-8
    char buf[5] = {0};
    int bytes = 1;

    if ((c & 0xE0) == 0xC0) bytes = 2;
    else if ((c & 0xF0) == 0xE0) bytes = 3;
    else if ((c & 0xF8) == 0xF0) bytes = 4;

    for (int i = 0; i < bytes && !at_end(); i++) {
        buf[i] = advance();
    }

    return create_text(buf, strlen(buf));
}

// =============================================================================
// Text Parsing
// =============================================================================

Item LatexParser::parse_text() {
    // parse regular text content until special character
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    const int MAX_TEXT_CHARS = 5000;  // safety limit
    int char_count = 0;

    while (!at_end() && char_count < MAX_TEXT_CHARS) {
        char c = peek();

        // check for special characters that end text
        if (c == '\\') {
            // check if escape or command
            if (pos_ + 1 < end_) {
                char next = pos_[1];
                // escaped special chars become text
                if (strchr("{}$&#_%", next)) {
                    advance();  // skip backslash
                    stringbuf_append_char(sb, advance());
                    char_count++;
                    continue;
                }
            }
            // command - break
            break;
        }

        if (c == '{' || c == '}' || c == '$' || c == '%') {
            break;
        }

        // handle dash ligatures
        if (c == '-') {
            if (pos_ + 2 < end_ && pos_[1] == '-' && pos_[2] == '-') {
                stringbuf_append_str(sb, "—");  // em dash
                advance(); advance(); advance();
                char_count += 3;
                continue;
            }
            if (pos_ + 1 < end_ && pos_[1] == '-') {
                stringbuf_append_str(sb, "–");  // en dash
                advance(); advance();
                char_count += 2;
                continue;
            }
            stringbuf_append_char(sb, advance());
            char_count++;
            continue;
        }

        // handle smart quotes
        if (c == '`') {
            if (pos_ + 1 < end_ && pos_[1] == '`') {
                stringbuf_append_str(sb, "\u201C");  // left double quote
                advance(); advance();
                char_count += 2;
                continue;
            }
            stringbuf_append_str(sb, "\u2018");  // left single quote
            advance();
            char_count++;
            continue;
        }
        if (c == '\'') {
            if (pos_ + 1 < end_ && pos_[1] == '\'') {
                stringbuf_append_str(sb, "\u201D");  // right double quote
                advance(); advance();
                char_count += 2;
                continue;
            }
            stringbuf_append_str(sb, "\u2019");  // right single quote
            advance();
            char_count++;
            continue;
        }

        // handle tilde as nbsp
        if (c == '~') {
            stringbuf_append_str(sb, "\u00A0");  // nbsp
            advance();
            char_count++;
            continue;
        }

        // paragraph break check
        if (c == '\n') {
            const char* look = pos_ + 1;
            while (look < end_ && (*look == ' ' || *look == '\t')) look++;
            if (look < end_ && *look == '\n') {
                // paragraph break - stop here
                break;
            }
            // single newline becomes space
            stringbuf_append_char(sb, ' ');
            advance();
            char_count++;
            continue;
        }

        // whitespace collapse
        if (c == ' ' || c == '\t') {
            stringbuf_append_char(sb, ' ');
            advance();
            // skip additional whitespace
            while (!at_end() && (peek() == ' ' || peek() == '\t')) {
                advance();
            }
            char_count++;
            continue;
        }

        // regular character - handle UTF-8
        if ((unsigned char)c >= 0x80) {
            // UTF-8 multi-byte
            int bytes = 1;
            if (((unsigned char)c & 0xE0) == 0xC0) bytes = 2;
            else if (((unsigned char)c & 0xF0) == 0xE0) bytes = 3;
            else if (((unsigned char)c & 0xF8) == 0xF0) bytes = 4;

            for (int i = 0; i < bytes && !at_end(); i++) {
                stringbuf_append_char(sb, advance());
            }
            char_count++;
            continue;
        }

        // regular ASCII char
        stringbuf_append_char(sb, advance());
        char_count++;
    }

    // normalize whitespace: trim trailing, collapse internal
    normalize_whitespace(sb);

    if (sb->length > 0) {
        // check if we stopped at paragraph break
        if (!at_end() && peek() == '\n') {
            const char* look = pos_ + 1;
            while (look < end_ && (*look == ' ' || *look == '\t')) look++;
            if (look < end_ && *look == '\n') {
                // create textblock with parbreak
                Item text_item = create_text(sb->str->chars, sb->length);

                // skip the paragraph break
                pos_ = look + 1;
                skip_whitespace();

                // wrap in textblock element
                Element* block = builder_.element("textblock").final().element;
                if (block && text_item.item != ITEM_NULL && text_item.item != ITEM_ERROR) {
                    list_push((List*)block, text_item);

                    Item parbreak = create_parbreak();
                    if (parbreak.item != ITEM_NULL && parbreak.item != ITEM_ERROR) {
                        list_push((List*)block, parbreak);
                    }

                    ((TypeElmt*)block->type)->content_length = ((List*)block)->length;
                    return Item{.element = block};
                }

                return text_item;
            }
        }

        return create_text(sb->str->chars, sb->length);
    }

    return ItemNull;
}

// Helper to normalize whitespace in text
void LatexParser::normalize_whitespace(StringBuf* sb) {
    if (sb->length == 0) return;

    // trim trailing whitespace
    while (sb->length > 0 &&
           (sb->str->chars[sb->length - 1] == ' ' ||
            sb->str->chars[sb->length - 1] == '\n' ||
            sb->str->chars[sb->length - 1] == '\r' ||
            sb->str->chars[sb->length - 1] == '\t')) {
        sb->length--;
    }
    sb->str->chars[sb->length] = '\0';
}

Item LatexParser::parse_paragraph() {
    // parse paragraph content
    return parse_text();
}

Item LatexParser::parse_text_block() {
    // parse text block with potential paragraph break
    return parse_text();
}

} // namespace latex
} // namespace lambda
