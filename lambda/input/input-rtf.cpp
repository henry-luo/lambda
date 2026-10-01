#include "input.hpp"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "lib/arraylist.h"
#include "lib/log.h"
#include "lib/stringbuf.h"
#include "../../lib/str.h"
#include <limits.h>

using namespace lambda;

static const int RTF_MAX_DEPTH = 512;

struct RTFControlWord {
    String* keyword;
    int parameter;
    bool has_parameter;
};

struct RTFFont {
    int number;
    String* name;
};

struct RTFColor {
    int red;
    int green;
    int blue;
    bool specified;
};

struct RTFStyle {
    int font;
    int size_half_points;
    int color;
    int align;
    int left_indent;
    int right_indent;
    int line_spacing;
    int unicode_fallback;
    int baseline;
    bool bold;
    bool italic;
    bool underline;
    bool strike;
    bool caps;
};

struct RTFParser {
    InputContext* ctx;
    const char* cursor;
    Element* document;
    Element* paragraph;
    StringBuf* text;
    ArrayList* fonts;
    ArrayList* colors;
    int default_font;
};

static RTFControlWord rtf_control_word(InputContext& ctx, const char** cursor) {
    RTFControlWord word = {};
    if (**cursor != '\\') return word;
    (*cursor)++;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    while (str_char_is_alpha(**cursor)) {
        stringbuf_append_char(sb, **cursor);
        (*cursor)++;
    }
    word.keyword = ctx.builder.createString(sb->str->chars, sb->length);
    if (**cursor == '-' || (**cursor >= '0' && **cursor <= '9')) {
        word.has_parameter = true;
        int sign = 1;
        if (**cursor == '-') { sign = -1; (*cursor)++; }
        while (**cursor >= '0' && **cursor <= '9') {
            int digit = **cursor - '0';
            word.parameter = word.parameter > (INT_MAX - digit) / 10
                ? INT_MAX : word.parameter * 10 + digit;
            (*cursor)++;
        }
        word.parameter *= sign;
    }
    if (**cursor == ' ') (*cursor)++;
    return word;
}

static bool rtf_word_is(const RTFControlWord& word, const char* keyword) {
    return word.keyword && strcmp(word.keyword->chars, keyword) == 0;
}

// The cursor is inside a destination group. Escaped braces do not affect depth.
static void rtf_skip_group_tail(RTFParser* parser) {
    int nested = 0;
    while (*parser->cursor) {
        if (*parser->cursor == '\\' && parser->cursor[1] &&
            (parser->cursor[1] == '{' || parser->cursor[1] == '}' ||
             parser->cursor[1] == '\\')) {
            parser->cursor += 2;
        } else if (*parser->cursor == '{') {
            nested++;
            parser->cursor++;
        } else if (*parser->cursor == '}') {
            parser->cursor++;
            if (nested == 0) return;
            nested--;
        } else {
            parser->cursor++;
        }
    }
}

static void rtf_skip_group(RTFParser* parser) {
    if (*parser->cursor == '{') parser->cursor++;
    rtf_skip_group_tail(parser);
}

static void rtf_store_font(RTFParser* parser, RTFFont* font, StringBuf* name) {
    if (!font || name->length == 0) return;
    size_t start = 0;
    while (start < name->length && name->str->chars[start] == ' ') start++;
    size_t end = name->length;
    while (end > start && name->str->chars[end - 1] == ' ') end--;
    if (end > start) {
        font->name = parser->ctx->builder.createString(name->str->chars + start, end - start);
        arraylist_append(parser->fonts, font);
    }
    stringbuf_reset(name);
}

static void rtf_parse_font_table(RTFParser* parser) {
    StringBuf* name = stringbuf_new(parser->ctx->input()->pool);
    if (!name) { rtf_skip_group_tail(parser); return; }
    RTFFont* font = nullptr;
    int nested = 0;
    while (*parser->cursor) {
        char ch = *parser->cursor;
        if (ch == '{') { nested++; parser->cursor++; continue; }
        if (ch == '}') {
            rtf_store_font(parser, font, name);
            font = nullptr;
            parser->cursor++;
            if (nested == 0) break;
            nested--;
            continue;
        }
        if (ch == '\\') {
            RTFControlWord word = rtf_control_word(*parser->ctx, &parser->cursor);
            if (rtf_word_is(word, "f") && word.has_parameter) {
                rtf_store_font(parser, font, name);
                font = (RTFFont*)pool_calloc(parser->ctx->input()->pool, sizeof(RTFFont));
                if (font) font->number = word.parameter;
            }
            continue;
        }
        parser->cursor++;
        if (ch == ';') {
            rtf_store_font(parser, font, name);
            font = nullptr;
        } else if (font && ch != '\r' && ch != '\n') {
            stringbuf_append_char(name, ch);
        }
    }
}

static void rtf_store_color(RTFParser* parser, const RTFColor& color) {
    RTFColor* stored = (RTFColor*)pool_calloc(parser->ctx->input()->pool, sizeof(RTFColor));
    if (!stored) return;
    *stored = color;
    arraylist_append(parser->colors, stored);
}

static void rtf_parse_color_table(RTFParser* parser) {
    RTFColor color = {};
    while (*parser->cursor) {
        char ch = *parser->cursor;
        if (ch == '}') { parser->cursor++; break; }
        if (ch == '\\') {
            RTFControlWord word = rtf_control_word(*parser->ctx, &parser->cursor);
            if (word.has_parameter) {
                if (rtf_word_is(word, "red")) { color.red = word.parameter; color.specified = true; }
                else if (rtf_word_is(word, "green")) { color.green = word.parameter; color.specified = true; }
                else if (rtf_word_is(word, "blue")) { color.blue = word.parameter; color.specified = true; }
            }
            continue;
        }
        parser->cursor++;
        if (ch == ';') {
            rtf_store_color(parser, color);
            color = {};
        }
    }
}

static const char* rtf_font_name(RTFParser* parser, int index) {
    for (int i = 0; i < parser->fonts->length; i++) {
        RTFFont* font = (RTFFont*)parser->fonts->data[i];
        if (font && font->number == index && font->name) return font->name->chars;
    }
    return nullptr;
}

static int rtf_color_channel(int value) {
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

static void rtf_append_style(RTFParser* parser, const RTFStyle& style) {
    StringBuf* css = parser->ctx->sb;
    stringbuf_reset(css);
    const char* font_name = rtf_font_name(parser, style.font);
    if (font_name) {
        stringbuf_append_str(css, "font-family:'");
        // Font names are data; accept only CSS family-name characters here.
        for (const char* ch = font_name; *ch; ch++) {
            if (str_char_is_alnum(*ch) || *ch == ' ' || *ch == '-') {
                stringbuf_append_char(css, *ch);
            }
        }
        stringbuf_append_str(css, "';");
    }
    if (style.size_half_points > 0) {
        stringbuf_append_format(css, "font-size:%.2fpx;", style.size_half_points * (2.0 / 3.0));
    }
    if (style.bold) stringbuf_append_str(css, "font-weight:700;");
    if (style.italic) stringbuf_append_str(css, "font-style:italic;");
    if (style.underline || style.strike) {
        stringbuf_append_str(css, "text-decoration:");
        if (style.underline) stringbuf_append_str(css, "underline");
        if (style.underline && style.strike) stringbuf_append_char(css, ' ');
        if (style.strike) stringbuf_append_str(css, "line-through");
        stringbuf_append_char(css, ';');
    }
    if (style.caps) stringbuf_append_str(css, "font-variant:small-caps;");
    if (style.baseline != 0) {
        stringbuf_append_str(css, style.baseline > 0
            ? "vertical-align:super;font-size:smaller;"
            : "vertical-align:sub;font-size:smaller;");
    }
    if (style.color >= 0 && style.color < parser->colors->length) {
        RTFColor* color = (RTFColor*)parser->colors->data[style.color];
        if (color && color->specified) {
            stringbuf_append_format(css, "color:rgb(%d,%d,%d);",
                rtf_color_channel(color->red), rtf_color_channel(color->green),
                rtf_color_channel(color->blue));
        }
    }
}

static Element* rtf_ensure_paragraph(RTFParser* parser, const RTFStyle& style) {
    if (parser->paragraph) return parser->paragraph;
    StringBuf* css = parser->ctx->sb;
    stringbuf_reset(css);
    const char* align = style.align == 1 ? "center" :
        (style.align == 2 ? "right" : (style.align == 3 ? "justify" : "left"));
    stringbuf_append_format(css, "text-align:%s;", align);
    if (style.left_indent > 0) {
        stringbuf_append_format(css, "margin-left:%.2fpx;", style.left_indent / 15.0);
    }
    if (style.right_indent > 0) {
        stringbuf_append_format(css, "margin-right:%.2fpx;", style.right_indent / 15.0);
    }
    if (style.line_spacing > 0) {
        stringbuf_append_format(css, "line-height:%.2fpx;", style.line_spacing / 15.0);
    }
    ElementBuilder paragraph = parser->ctx->builder.element("p");
    paragraph.attr("style", css->str->chars);
    parser->paragraph = paragraph.final().element;
    array_append((Array*)parser->document, {.element = parser->paragraph},
        parser->ctx->input()->pool, parser->ctx->input()->arena);
    return parser->paragraph;
}

static void rtf_flush_text(RTFParser* parser, const RTFStyle& style) {
    if (parser->text->length == 0) return;
    Element* paragraph = rtf_ensure_paragraph(parser, style);
    rtf_append_style(parser, style);
    ElementBuilder span = parser->ctx->builder.element("span");
    span.attr("style", parser->ctx->sb->str->chars);
    Element* element = span.final().element;
    Item text = parser->ctx->builder.createStringItem(parser->text->str->chars,
                                                       parser->text->length);
    array_append((Array*)element, text, parser->ctx->input()->pool,
                 parser->ctx->input()->arena);
    array_append((Array*)paragraph, {.element = element}, parser->ctx->input()->pool,
                 parser->ctx->input()->arena);
    stringbuf_reset(parser->text);
}

static void rtf_finish_paragraph(RTFParser* parser, const RTFStyle& style) {
    rtf_flush_text(parser, style);
    parser->paragraph = nullptr;
}

static bool rtf_hex_digit(char ch, int* value) {
    if (ch >= '0' && ch <= '9') { *value = ch - '0'; return true; }
    if (ch >= 'a' && ch <= 'f') { *value = ch - 'a' + 10; return true; }
    if (ch >= 'A' && ch <= 'F') { *value = ch - 'A' + 10; return true; }
    return false;
}

static void rtf_skip_unicode_fallback(RTFParser* parser, int count) {
    // RTF counts escaped characters such as \'hh as one fallback character.
    for (int skipped = 0; skipped < count && *parser->cursor; skipped++) {
        const char* cursor = parser->cursor;
        if (*cursor == '{' || *cursor == '}') return;
        if (*cursor != '\\') { parser->cursor++; continue; }
        int high = 0, low = 0;
        if (cursor[1] == '\'' && rtf_hex_digit(cursor[2], &high) &&
            rtf_hex_digit(cursor[3], &low)) {
            parser->cursor += 4;
        } else if (cursor[1] == '\\' || cursor[1] == '{' || cursor[1] == '}' ||
                   cursor[1] == '~' || cursor[1] == '-' || cursor[1] == '_') {
            parser->cursor += 2;
        } else {
            return;
        }
    }
}

static void rtf_parse_escape(RTFParser* parser, RTFStyle* style) {
    const char* cursor = parser->cursor;
    char symbol = cursor[1];
    if (symbol == '\\' || symbol == '{' || symbol == '}') {
        stringbuf_append_char(parser->text, symbol);
        parser->cursor += 2;
        return;
    }
    if (symbol == '\'') {
        int high = 0, low = 0;
        if (rtf_hex_digit(cursor[2], &high) && rtf_hex_digit(cursor[3], &low)) {
            unsigned int codepoint = (unsigned int)(high * 16 + low);
            stringbuf_append_utf8(parser->text, codepoint);
            parser->cursor += 4;
        } else { parser->cursor += 2; }
        return;
    }
    if (symbol == '~') { stringbuf_append_utf8(parser->text, 0xA0); parser->cursor += 2; return; }
    if (symbol == '-') { stringbuf_append_utf8(parser->text, 0xAD); parser->cursor += 2; return; }
    if (!str_char_is_alpha(symbol)) { parser->cursor += symbol ? 2 : 1; return; }

    rtf_flush_text(parser, *style);
    RTFControlWord word = rtf_control_word(*parser->ctx, &parser->cursor);
    int value = word.has_parameter ? word.parameter : 1;
    if (rtf_word_is(word, "par") || rtf_word_is(word, "row")) {
        rtf_finish_paragraph(parser, *style);
    } else if (rtf_word_is(word, "line")) {
        stringbuf_append_char(parser->text, '\n');
    } else if (rtf_word_is(word, "tab") || rtf_word_is(word, "cell")) {
        stringbuf_append_char(parser->text, '\t');
    } else if (rtf_word_is(word, "bullet")) {
        stringbuf_append_utf8(parser->text, 0x2022);
    } else if (rtf_word_is(word, "u") && word.has_parameter) {
        // RTF's \u value is signed UTF-16; the following ANSI fallback is skipped.
        stringbuf_append_utf8(parser->text, (uint16_t)word.parameter);
        rtf_skip_unicode_fallback(parser, style->unicode_fallback);
    } else if (rtf_word_is(word, "uc") && word.has_parameter) {
        style->unicode_fallback = value < 0 ? 0 : value;
    } else if (rtf_word_is(word, "deff") && word.has_parameter) {
        parser->default_font = value;
        style->font = value;
    } else if (rtf_word_is(word, "pard")) {
        style->align = 0; style->left_indent = 0;
        style->right_indent = 0; style->line_spacing = 0;
    } else if (rtf_word_is(word, "plain")) {
        style->font = parser->default_font; style->size_half_points = 24; style->color = 0;
        style->bold = false; style->italic = false; style->underline = false;
        style->strike = false; style->caps = false; style->baseline = 0;
    } else if (rtf_word_is(word, "ql")) style->align = 0;
    else if (rtf_word_is(word, "qc")) style->align = 1;
    else if (rtf_word_is(word, "qr")) style->align = 2;
    else if (rtf_word_is(word, "qj")) style->align = 3;
    else if (rtf_word_is(word, "li") && word.has_parameter) style->left_indent = value;
    else if (rtf_word_is(word, "ri") && word.has_parameter) style->right_indent = value;
    else if (rtf_word_is(word, "sl") && word.has_parameter) style->line_spacing = value;
    else if (rtf_word_is(word, "f") && word.has_parameter) style->font = value;
    else if (rtf_word_is(word, "fs") && word.has_parameter) style->size_half_points = value;
    else if (rtf_word_is(word, "cf") && word.has_parameter) style->color = value;
    else if (rtf_word_is(word, "b")) style->bold = value != 0;
    else if (rtf_word_is(word, "i")) style->italic = value != 0;
    else if (rtf_word_is(word, "ul")) style->underline = value != 0;
    else if (rtf_word_is(word, "ulnone")) style->underline = false;
    else if (rtf_word_is(word, "strike")) style->strike = value != 0;
    else if (rtf_word_is(word, "caps")) style->caps = value != 0;
    else if (rtf_word_is(word, "super")) style->baseline = 1;
    else if (rtf_word_is(word, "sub")) style->baseline = -1;
    else if (rtf_word_is(word, "nosupersub")) style->baseline = 0;
    else if (rtf_word_is(word, "footnote") && *parser->cursor == '{') {
        // A footnote destination is separate from its reference paragraph.
        rtf_skip_group(parser);
    }
    else if (rtf_word_is(word, "page")) {
        rtf_finish_paragraph(parser, *style);
        Element* separator = parser->ctx->builder.createElement("hr").element;
        array_append((Array*)parser->document, {.element = separator},
            parser->ctx->input()->pool, parser->ctx->input()->arena);
    }
}

static void rtf_parse_group(RTFParser* parser, RTFStyle style, int depth) {
    if (*parser->cursor != '{') return;
    if (depth >= RTF_MAX_DEPTH) {
        parser->ctx->addError(parser->ctx->tracker.location(),
            "Maximum RTF nesting depth (%d) exceeded", RTF_MAX_DEPTH);
        rtf_skip_group(parser);
        return;
    }
    parser->cursor++;
    while (*parser->cursor == ' ' || *parser->cursor == '\t' ||
           *parser->cursor == '\r' || *parser->cursor == '\n') parser->cursor++;
    if (parser->cursor[0] == '\\' && parser->cursor[1] == '*') {
        parser->cursor += 2;
        rtf_skip_group_tail(parser);
        return;
    }
    bool at_group_start = true;
    while (*parser->cursor) {
        char ch = *parser->cursor;
        if (ch == '}') {
            rtf_flush_text(parser, style);
            parser->cursor++;
            return;
        }
        if (ch == '{') {
            at_group_start = false;
            rtf_flush_text(parser, style);
            rtf_parse_group(parser, style, depth + 1);
            continue;
        }
        if (at_group_start && ch == '\\' && str_char_is_alpha(parser->cursor[1])) {
            const char* saved = parser->cursor;
            RTFControlWord word = rtf_control_word(*parser->ctx, &parser->cursor);
            if (rtf_word_is(word, "fonttbl")) { rtf_parse_font_table(parser); return; }
            if (rtf_word_is(word, "colortbl")) { rtf_parse_color_table(parser); return; }
            if (rtf_word_is(word, "stylesheet") || rtf_word_is(word, "info") ||
                rtf_word_is(word, "header") || rtf_word_is(word, "footer") ||
                rtf_word_is(word, "fldinst") || rtf_word_is(word, "pict") ||
                rtf_word_is(word, "object") || rtf_word_is(word, "footnote")) {
                rtf_skip_group_tail(parser);
                return;
            }
            parser->cursor = saved;
        }
        at_group_start = false;
        if (ch == '\\') { rtf_parse_escape(parser, &style); continue; }
        parser->cursor++;
        // RTF source line endings are separators, not displayed line breaks.
        if (ch != '\r' && ch != '\n') stringbuf_append_char(parser->text, ch);
    }
    rtf_flush_text(parser, style);
}

void parse_rtf(Input* input, const char* source) {
    if (!source || !*source) { input->root = {.item = ITEM_NULL}; return; }
    InputContext ctx(input, source, strlen(source));
    const char* cursor = source;
    while (*cursor == ' ' || *cursor == '\r' || *cursor == '\n') cursor++;
    if (strncmp(cursor, "{\\rtf", 5) != 0) {
        ctx.addError(ctx.tracker.location(), "Invalid RTF format: document must start with '{\\rtf'");
        input->root = {.item = ITEM_ERROR};
        return;
    }
    RTFParser parser = {};
    parser.ctx = &ctx;
    parser.cursor = cursor;
    parser.document = ctx.builder.createElement("doc").element;
    parser.text = stringbuf_new(input->pool);
    parser.fonts = arraylist_new(8);
    parser.colors = arraylist_new(8);
    if (!parser.document || !parser.text || !parser.fonts || !parser.colors) {
        if (parser.fonts) arraylist_free(parser.fonts);
        if (parser.colors) arraylist_free(parser.colors);
        ctx.addError(ctx.tracker.location(), "Unable to allocate RTF document state");
        input->root = {.item = ITEM_ERROR};
        return;
    }
    RTFStyle style = {};
    style.size_half_points = 24;
    style.unicode_fallback = 1;
    rtf_parse_group(&parser, style, 0);
    rtf_finish_paragraph(&parser, style);
    arraylist_free(parser.fonts);
    arraylist_free(parser.colors);
    if (ctx.hasErrors()) ctx.logErrors();
    input->root = {.element = parser.document};
}
