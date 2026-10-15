// Wiki inline syntax is selected by flavor; opaque macros retain their source.
#include "inline_common.hpp"
#include "../wiki.hpp"
#include "lib/arraylist.h"
#include "lib/str.h"
#include "lib/strbuf.h"
#include <cstring>

namespace lambda {
namespace markup {

static bool scan_wiki_braces(const char* start, int opening_width,
                             const char** content_end, const char** source_end,
                             const char** first_pipe) {
    ArrayList* widths = arraylist_new(8);
    if (!widths) return false;
    if (!arraylist_append(widths, (ArrayListValue)(intptr_t)opening_width)) {
        arraylist_free(widths);
        return false;
    }
    const char* pos = start + opening_width;
    int link_depth = 0;
    *first_pipe = nullptr;
    *content_end = nullptr;
    *source_end = nullptr;

    while (*pos && pos - start <= 10000) {
        if (pos[0] == '[' && pos[1] == '[') { link_depth++; pos += 2; continue; }
        if (pos[0] == ']' && pos[1] == ']' && link_depth) { link_depth--; pos += 2; continue; }
        if (link_depth) { pos++; continue; }

        if (pos[0] == '{' && pos[1] == '{') {
            int width = pos[2] == '{' ? 3 : 2;
            if (!arraylist_append(widths, (ArrayListValue)(intptr_t)width)) {
                arraylist_free(widths);
                return false;
            }
            pos += width;
            continue;
        }
        int width = (int)(intptr_t)widths->data[widths->length - 1];
        if (pos[0] == '}' && pos[1] == '}' && (width == 2 || pos[2] == '}')) {
            arraylist_pop(widths);
            if (widths->length == 0) {
                *content_end = pos;
                *source_end = pos + width;
                break;
            }
            pos += width;
            continue;
        }
        if (*pos == '|' && widths->length == 1 && !*first_pipe) *first_pipe = pos;
        pos++;
    }
    arraylist_free(widths);
    return *content_end != nullptr;
}

/**
 * parse_wiki_template - Parse MediaWiki templates and template parameters
 */
Item parse_wiki_template(MarkupParser* parser, const char** text) {
    const char* start = *text;
    if (start[0] != '{' || start[1] != '{') return Item{.item = ITEM_UNDEFINED};

    int opening_width = start[2] == '{' ? 3 : 2;
    const char* content_end;
    const char* source_end;
    const char* first_pipe;
    if (!scan_wiki_braces(start, opening_width, &content_end, &source_end, &first_pipe)) {
        return Item{.item = ITEM_UNDEFINED};
    }

    Element* value = create_element(parser, "var");
    if (!value) {
        *text = source_end;
        return Item{.item = ITEM_ERROR};
    }
    add_attribute_to_element(parser, value,
                             opening_width == 3 ? "data-wiki-parameter" : "data-wiki-template", "true");

    // the source is visible when no expansion context is available.
    char* source = mem_dup_n(start, source_end - start, MEM_CAT_INPUT_MARKUP);
    if (source) {
        add_attribute_to_element(parser, value, "source", source);
        String* visible = parser->builder.createString(source);
        if (visible) list_push((List*)value, Item{.item = s2it(visible)});
        mem_free(source);
    }

    const char* name_start = start + opening_width;
    const char* name_end = first_pipe ? first_pipe : content_end;
    const char* arguments_start = first_pipe ? first_pipe + 1 : nullptr;
    if (opening_width == 2 && *name_start == '#') {
        // parser functions put their first argument after a colon in the title.
        const char* colon = (const char*)memchr(name_start, ':', name_end - name_start);
        if (colon) {
            name_end = colon;
            arguments_start = colon + 1;
            add_attribute_to_element(parser, value, "data-wiki-parser-function", "true");
        }
    }
    size_t name_len = name_end - name_start;
    str_trim(&name_start, &name_len);
    char* name = mem_dup_n(name_start, name_len, MEM_CAT_INPUT_MARKUP);
    if (name) {
        add_attribute_to_element(parser, value, "name", name);
        mem_free(name);
    }
    if (arguments_start) {
        const char* attr = opening_width == 3 ? "default" : "args";
        char* arguments = mem_dup_n(arguments_start, content_end - arguments_start, MEM_CAT_INPUT_MARKUP);
        if (arguments) {
            add_attribute_to_element(parser, value, attr, arguments);
            mem_free(arguments);
        }
    }

    *text = source_end;
    return Item{.item = (uint64_t)value};
}

Item parse_wiki_nowiki(MarkupParser* parser, const char** text) {
    const char* start = *text;
    if (strncmp(start, "<nowiki>", 8) != 0) return Item{.item = ITEM_UNDEFINED};
    const char* content = start + 8;
    const char* close = strstr(content, "</nowiki>");
    if (!close) return Item{.item = ITEM_UNDEFINED};

    Element* literal = create_element(parser, "span");
    if (!literal) return Item{.item = ITEM_ERROR};
    add_attribute_to_element(parser, literal, "data-wiki-nowiki", "true");
    char* source = mem_dup_n(start, close + 9 - start, MEM_CAT_INPUT_MARKUP);
    if (source) {
        add_attribute_to_element(parser, literal, "source", source);
        mem_free(source);
    }
    String* visible = parser->builder.createString(content, close - content);
    if (visible) list_push((List*)literal, Item{.item = s2it(visible)});
    *text = close + 9;
    return Item{.item = (uint64_t)literal};
}

const char* wiki_slice(MarkupParser* parser, const char* begin, size_t length) {
    String* value = parser->builder.createString(begin, length);
    return value ? value->chars : "";
}

Item wiki_text(MarkupParser* parser, const char* begin, size_t length) {
    String* value = parser->builder.createString(begin, length);
    return value ? Item{.item = s2it(value)} : ItemError;
}

Item wiki_raw(MarkupParser* parser, const char* begin, size_t length, const char* kind) {
    Element* raw = create_element(parser, "raw");
    add_attribute_to_element(parser, raw, "format", wiki_syntax(parser->config.flavor)->name);
    add_attribute_to_element(parser, raw, "data-kind", kind);
    add_attribute_to_element(parser, raw, "data-diagnostic", "unsupported-wiki-construct");
    list_push((List*)raw, wiki_text(parser, begin, length));
    parser->addWarning("wiki input: preserved unsupported %s", kind);
    return Item{.element = raw};
}

static bool wiki_name_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
}

bool wiki_macro_header(MarkupParser* parser, const char* source, WikiMacro* macro) {
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    bool xwiki = flavor == Flavor::XWIKI;
    bool tiki = flavor == Flavor::TIKIWIKI;
    if ((!xwiki && !tiki && !wiki_is_atlassian(flavor)) || *source != '{' ||
        (xwiki && source[1] != '{') || (!xwiki && source[1] == '{')) return false;
    const char* p = source + (xwiki ? 2 : 1);
    macro->name = p;
    while (wiki_name_char(*p)) p++;
    if (p == macro->name) return false;
    macro->name_length = p - macro->name;
    macro->arguments = p;
    char quote = 0;
    while (*p) {
        if (quote) { if (*p == quote && p[-1] != '\\') quote = 0; }
        else if ((*p == '"' || *p == '\'') && (p == macro->arguments || p[-1] == '=' ||
            p[-1] == ':' || str_is_space(p[-1]))) quote = *p;
        else if (*p == '}' && (!xwiki || p[1] == '}')) break;
        p++;
    }
    if (!*p) return false;
    macro->arguments_length = p - macro->arguments;
    macro->header_end = p + (xwiki ? 2 : 1);
    bool standalone = p > macro->arguments && p[-1] == '/';
    if (standalone) macro->arguments_length--;
    if ((macro->name_length == 6 && !strncasecmp(macro->name, "anchor", 6)) ||
        (macro->name_length == 3 && !strncasecmp(macro->name, "img", 3)) ||
        (xwiki && macro->name_length == 2 && !strncasecmp(macro->name, "id", 2))) standalone = true;
    if (tiki && macro->name[0] >= 'a' && macro->name[0] <= 'z') standalone = true;
    if (!standalone) {
        StrBuf* close = strbuf_new();
        strbuf_append_str(close, xwiki ? "{{/" : "{");
        strbuf_append_str_n(close, macro->name, macro->name_length);
        strbuf_append_str(close, xwiki ? "}}" : "}");
        macro->close = wiki_slice(parser, close->str, close->length);
        strbuf_free(close);
    }
    return true;
}

bool wiki_next_parameter(const char** cursor, const char* end, WikiParameter* parameter, bool pipe_separated) {
    const char* p = *cursor;
    while (p < end && (str_is_space(*p) || strchr(":|,();/", *p))) p++;
    if (p == end) return false;
    *parameter = {};
    const char* name = p;
    while (p < end && wiki_name_char(*p)) p++;
    const char* name_end = p;
    while (p < end && str_is_space(*p)) p++;
    if (name_end > name && p < end && *p == '=') {
        parameter->key = name; parameter->key_length = name_end - name;
        p++; while (p < end && str_is_space(*p)) p++;
    } else p = name;
    char quote = p < end && (*p == '"' || *p == '\'') ? *p++ : 0;
    parameter->value = p;
    while (p < end && (quote ? *p != quote : pipe_separated ? *p != '|' : !str_is_space(*p) && !strchr("|,)", *p))) p++;
    parameter->value_length = p - parameter->value;
    while (!quote && parameter->value_length && str_is_space(parameter->value[parameter->value_length-1])) parameter->value_length--;
    if (quote && p < end) p++;
    *cursor = p;
    return true;
}

const char* wiki_macro_parameter(MarkupParser* parser, const WikiMacro& macro, const char* key) {
    const char* cursor = macro.arguments;
    const char* end = cursor + macro.arguments_length;
    WikiParameter parameter;
    while (wiki_next_parameter(&cursor, end, &parameter, wiki_is_atlassian(parser->config.flavor))) {
        bool named = parameter.key && parameter.key_length == strlen(key) &&
            !strncmp(parameter.key, key, parameter.key_length);
        bool language = !parameter.key && !strcmp(key, "language") && wiki_is_atlassian(parser->config.flavor);
        if (named || language) return wiki_slice(parser, parameter.value, parameter.value_length);
    }
    return nullptr;
}

static bool macro_named(const WikiMacro& macro, const char* name) {
    return strlen(name) == macro.name_length &&
        strncasecmp(macro.name, name, macro.name_length) == 0;
}

const char* wiki_macro_body_end(MarkupParser* parser, const WikiMacro& macro) {
    if (!macro.close) return nullptr;
    if (macro_named(macro, "code") || macro_named(macro, "noformat"))
        return strstr(macro.header_end, macro.close);
    unsigned depth = 1;
    size_t close_length = strlen(macro.close);
    for (const char* p = macro.header_end; *p;) {
        if (!strncmp(p, macro.close, close_length)) {
            if (!--depth) return p;
            p += close_length;
            continue;
        }
        WikiMacro nested;
        if (*p == '{' && wiki_macro_header(parser, p, &nested)) {
            if (nested.name_length == macro.name_length &&
                !strncasecmp(nested.name, macro.name, macro.name_length) && nested.close) depth++;
            else if (nested.close && (macro_named(nested, "code") || macro_named(nested, "noformat"))) {
                const char* end = strstr(nested.header_end, nested.close);
                if (end) { p = end + strlen(nested.close); continue; }
            }
            p = nested.header_end;
        } else p++;
    }
    return nullptr;
}

Item parse_wiki_macro(MarkupParser* parser, const char** source, bool blocks) {
    const char* start = *source;
    WikiMacro macro;
    if (!wiki_macro_header(parser, start, &macro)) return Item{.item = ITEM_UNDEFINED};
    const char* body_end = wiki_macro_body_end(parser, macro);
    const char* source_end = body_end ? body_end + strlen(macro.close) : macro.header_end;
    *source = source_end;
    bool code = macro_named(macro, "code") || macro_named(macro, "noformat");
    bool quote = macro_named(macro, "quote");
    bool panel = macro_named(macro, "panel") || macro_named(macro, "box");
    bool color = macro_named(macro, "color");
    bool anchor = macro_named(macro, "anchor") ||
        (parser->config.flavor == Flavor::XWIKI && macro_named(macro, "id"));
    bool image = macro_named(macro, "img");
    if (parser->config.flavor == Flavor::XWIKI && !code && !anchor)
        return wiki_raw(parser, start, source_end - start, "macro");
    if ((!body_end && (code || quote || panel || color)) ||
        !(code || quote || panel || color || anchor || image))
        return wiki_raw(parser, start, source_end - start, "macro");
    // Preserve an entire static macro when a parameter has no schema mapping.
    const char* parameters = macro.arguments;
    WikiParameter parameter;
    while (wiki_next_parameter(&parameters, macro.arguments + macro.arguments_length, &parameter,
            wiki_is_atlassian(parser->config.flavor))) {
        const char* key = parameter.key ? wiki_slice(parser, parameter.key, parameter.key_length) : "";
        bool supported = code ? !strcmp(key, "language") || (!*key && wiki_is_atlassian(parser->config.flavor)) :
            panel ? !strcmp(key, "title") : image ? !strcmp(key, "src") || !strcmp(key, "alt") :
            anchor ? !*key || !strcmp(key, "name") : color ? !*key : false;
        if (!supported) return wiki_raw(parser, start, source_end - start, "macro-parameters");
    }
    Element* value = create_element(parser, code ? "code" : quote ? "blockquote" :
        panel ? "div" : image ? "img" : "span");
    if (code) {
        add_attribute_to_element(parser, value, "type", "block");
        const char* language = wiki_macro_parameter(parser, macro, "language");
        if (language && *language) add_attribute_to_element(parser, value, "language", language);
        const char* body = macro.header_end;
        if (*body == '\n') body++;
        list_push((List*)value, wiki_text(parser, body, body_end - body));
    } else if (anchor) {
        const char* id = macro.arguments;
        if (*id == ':') id++;
        const char* named_id = wiki_macro_parameter(parser, macro, "name");
        add_attribute_to_element(parser, value, "id", named_id ? named_id : wiki_slice(parser, id,
            macro.arguments + macro.arguments_length - id));
    } else if (image) {
        const char* src = wiki_macro_parameter(parser, macro, "src");
        if (!src) return wiki_raw(parser, start, source_end - start, "image-macro");
        add_attribute_to_element(parser, value, "src", src);
        const char* alt = wiki_macro_parameter(parser, macro, "alt");
        if (alt) add_attribute_to_element(parser, value, "alt", alt);
    } else {
        if (panel) {
            add_attribute_to_element(parser, value, "class", "wiki-panel");
            const char* title = wiki_macro_parameter(parser, macro, "title");
            if (title) {
                Element* heading = create_element(parser, "p");
                add_attribute_to_element(parser, heading, "class", "wiki-panel-title");
                list_push((List*)heading, wiki_text(parser, title, strlen(title)));
                list_push((List*)value, Item{.element = heading});
            }
        } else if (color) {
            const char* color_value = macro.arguments;
            if (*color_value == ':') color_value++;
            add_attribute_to_element(parser, value, "data-color", wiki_slice(parser,
                color_value, macro.arguments + macro.arguments_length - color_value));
        }
        wiki_append_fragment(parser, value, macro.header_end, body_end - macro.header_end,
            !color && (quote || panel || blocks));
    }
    return Item{.element = value};
}

static Item wiki_link_or_image(MarkupParser* parser, const char** source) {
    FormatAdapter* adapter = parser->adapter();
    LinkInfo info = adapter->detectImage(*source);
    bool image = info.valid;
    if (!image) info = adapter->detectLink(*source);
    if (!info.valid) return Item{.item = ITEM_UNDEFINED};
    if (parser->state.link_depth && !image) return Item{.item = ITEM_UNDEFINED};
    const char* original = *source;
    bool parameters = parser->config.flavor == Flavor::XWIKI && !strncmp(info.url_end, "||", 2);
    if (image && (parser->config.flavor == Flavor::DOKUWIKI || parser->config.flavor == Flavor::ZIMWIKI))
        parameters = memchr(info.url_start, '?', info.url_end - info.url_start) != nullptr;
    if (parameters) {
        *source = info.end_pos;
        return wiki_raw(parser, original, info.end_pos - original, image ? "image-options" : "link-parameters");
    }
    if (parser->config.flavor == Flavor::XWIKI &&
        info.url_end - info.url_start >= 6 && !strncmp(info.url_start, "image:", 6)) {
        image = true; info.url_start += 6;
    }
    Element* value = create_element(parser, image ? "img" : "a");
    add_attribute_to_element(parser, value, image ? "src" : "href",
        wiki_slice(parser, info.url_start, info.url_end - info.url_start));
    if (image) {
        const char* alt = info.text_start;
        if (wiki_is_mediawiki(parser)) {
            for (const char* p = alt; p < info.text_end; p++) if (*p == '|') alt = p + 1;
        }
        if (wiki_is_atlassian(parser->config.flavor) && alt < info.text_end) {
            WikiMacro options; options.arguments = alt; options.arguments_length = info.text_end - alt;
            const char* label = wiki_macro_parameter(parser, options, "alt");
            if (label) add_attribute_to_element(parser, value, "alt", label);
            if (memchr(alt, ',', info.text_end - alt)) {
                *source = info.end_pos;
                return wiki_raw(parser, original, info.end_pos - original, "image-options");
            }
            const char* cursor = alt; WikiParameter parameter;
            while (wiki_next_parameter(&cursor, info.text_end, &parameter, true)) {
                if (!parameter.key || parameter.key_length != 3 || strncmp(parameter.key, "alt", 3)) {
                    *source = info.end_pos;
                    return wiki_raw(parser, info.url_start - 1, info.end_pos - info.url_start + 1, "image-options");
                }
            }
        } else add_attribute_to_element(parser, value, "alt", wiki_slice(parser, alt, info.text_end - alt));
    } else {
        const char* colon = (const char*)memchr(info.url_start, ':', info.url_end - info.url_start);
        if (colon && wiki_is_mediawiki(parser)) add_attribute_to_element(parser, value, "namespace",
            wiki_slice(parser, info.url_start, colon - info.url_start));
        parser->state.link_depth++;
        list_push((List*)value, parse_inline_spans(parser,
            wiki_slice(parser, info.text_start, info.text_end - info.text_start)));
        parser->state.link_depth--;
    }
    *source = info.end_pos;
    return Item{.element = value};
}

const char* wiki_find_close(const char* text, const char* close, char escape) {
    size_t width = strlen(close);
    for (const char* p = text; *p; p++) {
        if (*p == escape && p[1]) { p++; continue; }
        if (!strncmp(p, "<nowiki>", 8)) {
            const char* end = strstr(p + 8, "</nowiki>");
            if (end) { p = end + 8; continue; }
        }
        if (!strncmp(p, close, width)) return p;
    }
    return nullptr;
}

static Item wiki_html(MarkupParser* parser, const char** source) {
    const char* start = *source;
    if (*start != '<' || !wiki_name_char(start[1])) return Item{.item = ITEM_UNDEFINED};
    const char* name_end = start + 1;
    while (wiki_name_char(*name_end)) name_end++;
    const char* name = wiki_slice(parser, start + 1, name_end - start - 1);
    const char* header_end = name_end;
    char quote = 0;
    while (*header_end) {
        if (quote) { if (*header_end == quote) quote = 0; }
        else if (*header_end == '"' || *header_end == '\'') quote = *header_end;
        else if (*header_end == '>') break;
        header_end++;
    }
    if (!*header_end) return Item{.item = ITEM_UNDEFINED};
    StrBuf* closing = strbuf_new();
    strbuf_append_str(closing, "</"); strbuf_append_str(closing, name); strbuf_append_char(closing, '>');
    const char* body = header_end + 1;
    const char* end = strstr(body, closing->str);
    bool empty = !strcmp(name, "br") || !strcmp(name, "img") || header_end[-1] == '/';
    const char* source_end = !empty && end ? end + closing->length : body;
    strbuf_free(closing);
    static const char* tags[] = {"strong", "b", "em", "i", "s", "del", "u", "sup", "sub",
        "code", "pre", "verbatim", "literal", "nowiki", "span", "br", "img", "cite", "syntaxhighlight", "source"};
    bool supported = false;
    for (const char* tag : tags) if (!strcmp(name, tag)) supported = true;
    *source = source_end;
    if (!supported || (!empty && !end)) return wiki_raw(parser, start, source_end - start, "html");
    if (!strcmp(name, "literal") && end) return wiki_text(parser, body, end - body);
    bool literal = !strcmp(name, "code") || !strcmp(name, "pre") || !strcmp(name, "verbatim") ||
        !strcmp(name, "literal") || !strcmp(name, "nowiki") || !strcmp(name, "syntaxhighlight") || !strcmp(name, "source");
    Element* value = create_element(parser, literal ? "code" : !strcmp(name, "s") ? "del" : name);
    const char* cursor = name_end;
    WikiParameter parameter;
    while (wiki_next_parameter(&cursor, header_end, &parameter)) {
        if (!parameter.key) return wiki_raw(parser, start, source_end - start, "html-attributes");
        const char* key = wiki_slice(parser, parameter.key, parameter.key_length);
        bool allowed = !strcmp(key, "id") || !strcmp(key, "class") || !strcmp(key, "title") ||
            (!strcmp(name, "img") && (!strcmp(key, "src") || !strcmp(key, "alt"))) ||
            (literal && (!strcmp(key, "lang") || !strcmp(key, "language")));
        if (!allowed) return wiki_raw(parser, start, source_end - start, "html-attributes");
        add_attribute_to_element(parser, value, !strcmp(key, "lang") ? "language" : key,
            wiki_slice(parser, parameter.value, parameter.value_length));
    }
    if (literal) {
        add_attribute_to_element(parser, value, "type", !strcmp(name, "pre") || !strcmp(name, "verbatim") || !strcmp(name, "syntaxhighlight") || !strcmp(name, "source") ||
            (end && memchr(body, '\n', end - body)) ? "block" : "inline");
        if (end) list_push((List*)value, wiki_text(parser, body, end - body));
    } else if (!empty) list_push((List*)value, parse_wiki_inlines(parser, wiki_slice(parser, body, end - body)));
    return Item{.element = value};
}

static Item wiki_tag_or_anchor(MarkupParser* parser, const char** source, const char* text) {
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    const char* start = *source;
    bool boundary = start == text || str_is_space(start[-1]);
    if (!boundary) return Item{.item = ITEM_UNDEFINED};
    if (flavor == Flavor::ZIMWIKI && !strncmp(start, "##", 2) && wiki_name_char(start[2])) {
        const char* end = start + 2;
        while (wiki_name_char(*end)) end++;
        Element* anchor = create_element(parser, "span");
        add_attribute_to_element(parser, anchor, "id", wiki_slice(parser, start + 2, end - start - 2));
        *source = end; return Item{.element = anchor};
    }
    if (flavor != Flavor::VIMWIKI || *start != ':') return Item{.item = ITEM_UNDEFINED};
    Element* tags = create_element(parser, "span");
    const char* p = start;
    bool first = true;
    while (*p == ':') {
        const char* end = p + 1;
        while (*end && !str_is_space(*end) && *end != ':') end++;
        if (*end != ':' || end == p + 1) break;
        const char* label = wiki_slice(parser, p + 1, end - p - 1);
        Element* tag = create_element(parser, "span");
        add_attribute_to_element(parser, tag, "data-tag", label);
        add_attribute_to_element(parser, tag, "id", label);
        list_push((List*)tag, wiki_text(parser, first ? p : p + 1, end - p + (first ? 1 : 0)));
        list_push((List*)tags, Item{.element = tag});
        wiki_record_tag(parser, p + 1, end - p - 1);
        first = false; p = end;
    }
    if (first) return Item{.item = ITEM_UNDEFINED};
    *source = p + 1;
    if (parser->wiki_inline_line_ >= 2 || parser->wiki_block_depth_)
        return wiki_raw(parser, start, p + 1 - start, "local-tags");
    return Item{.element = tags};
}

static Item wiki_word(MarkupParser* parser, const char** source, const char* text) {
    const char* start = *source;
    if (parser->config.flavor != Flavor::TWIKI || parser->state.link_depth ||
        *start < 'A' || *start > 'Z' || (start > text && (wiki_name_char(start[-1]) || strchr(".@", start[-1]))))
        return Item{.item = ITEM_UNDEFINED};
    const char* end = start;
    bool lower = false, word = false;
    while (wiki_name_char(*end) || *end == '.' || *end == '#') {
        if (*end >= 'a' && *end <= 'z') lower = true;
        if (*end >= 'A' && *end <= 'Z' && lower) word = true;
        end++;
    }
    while (end > start && end[-1] == '.') end--;
    if (!word) return Item{.item = ITEM_UNDEFINED};
    Element* link = create_element(parser, "a");
    add_attribute_to_element(parser, link, "href", wiki_slice(parser, start, end - start));
    list_push((List*)link, wiki_text(parser, start, end - start));
    *source = end; return Item{.element = link};
}

Item parse_wiki_inlines(MarkupParser* parser, const char* text) {
    if (!text || !*text) return Item{.item = ITEM_UNDEFINED};
    const WikiSyntax* syntax = wiki_syntax(parser->config.flavor);
    // Bound recursive emphasis/link labels without truncating the source text.
    if (parser->wiki_inline_depth_ >= 128) return wiki_raw(parser, text, strlen(text), "nesting-limit");
    parser->wiki_inline_depth_++;
    size_t saved_line = parser->wiki_inline_line_;
    size_t source_line = saved_line;
    Element* span = create_element(parser, "span");
    const char* p = text;
    const char* plain = p;
    while (*p) {
        const char* start = p;
        Item token{.item = ITEM_UNDEFINED};
        bool escaped = *p == syntax->escape && p[1] &&
            (syntax->escape != '\\' || is_escapable(p[1]));
        bool slash_break = p[0] == '\\' && p[1] == '\\' &&
            (wiki_is_atlassian(syntax->flavor) || syntax->flavor == Flavor::CREOLE ||
             syntax->flavor == Flavor::XWIKI || syntax->flavor == Flavor::DOKUWIKI);
        if (slash_break || (*p == '\n' && (syntax->flavor == Flavor::CONFLUENCE || syntax->flavor == Flavor::ZIMWIKI))) {
            token = Item{.element = create_element(parser, "br")}; p += *p == '\n' ? 1 : 2;
        } else if (*p == '\n' && syntax->flavor == Flavor::VIMWIKI) {
            token = wiki_text(parser, " ", 1); p++;
        } else if (escaped) {
            token = wiki_text(parser, p + 1, 1); p += 2;
        } else if (*p == '[' || (*p == '(' && syntax->links == WikiLinks::TIKI) ||
            (*p == '{' && p[1] == '{') || (*p == '!' && syntax->links == WikiLinks::ATLASSIAN)) {
            token = wiki_link_or_image(parser, &p);
        }
        if (token.item == ITEM_UNDEFINED && *p == '<' && wiki_is_mediawiki(parser))
            token = parse_wiki_nowiki(parser, &p);
        if (token.item == ITEM_UNDEFINED && *p == '<' && (wiki_is_mediawiki(parser) ||
            syntax->flavor == Flavor::TWIKI || syntax->flavor == Flavor::DOKUWIKI)) token = wiki_html(parser, &p);
        if (token.item == ITEM_UNDEFINED && *p == '&' && (wiki_is_mediawiki(parser) ||
            syntax->flavor == Flavor::TWIKI || wiki_is_atlassian(syntax->flavor)))
            token = parse_entity_reference(parser, &p);
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::TIKIWIKI && !strncmp(p, "~np~", 4)) {
            const char* end = strstr(p + 4, "~/np~");
            if (end) { token = wiki_text(parser, p + 4, end - p - 4); p = end + 5; }
        }
        if (token.item == ITEM_UNDEFINED) token = wiki_tag_or_anchor(parser, &p, text);
        if (token.item == ITEM_UNDEFINED && !strncmp(p, "{{", 2) && wiki_is_mediawiki(parser))
            token = parse_wiki_template(parser, &p);
        if (token.item == ITEM_UNDEFINED && !strncmp(p, "{{{", 3) &&
            (syntax->flavor == Flavor::CREOLE || syntax->flavor == Flavor::VIMWIKI || syntax->flavor == Flavor::XWIKI)) {
            const char* end = strstr(p + 3, "}}}");
            if (end) {
                Element* literal = create_element(parser, "code");
                add_attribute_to_element(parser, literal, "type", "inline");
                list_push((List*)literal, wiki_text(parser, p + 3, end - p - 3));
                token = Item{.element = literal}; p = end + 3;
            }
        }
        if (token.item == ITEM_UNDEFINED && *p == '{')
            token = parse_wiki_macro(parser, &p, false);
        if (token.item == ITEM_UNDEFINED && ((syntax->flavor == Flavor::TWIKI && !strncmp(p, "%BR%", 4)) ||
            (syntax->flavor == Flavor::TIKIWIKI && !strncmp(p, "%%%", 3)))) {
            token = Item{.element = create_element(parser, "br")}; p += syntax->flavor == Flavor::TWIKI ? 4 : 3;
        }
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::DOKUWIKI && !strncmp(p, "%%", 2)) {
            const char* end = strstr(p + 2, "%%");
            if (end) { token = wiki_text(parser, p + 2, end - p - 2); p = end + 2; }
        }
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::TWIKI && *p == '%') {
            const char* name_end = p + 1;
            while ((*name_end >= 'A' && *name_end <= 'Z') || (*name_end >= '0' && *name_end <= '9') || *name_end == '_') name_end++;
            if (name_end > p + 1 && (*name_end == '%' || *name_end == '{')) {
                const char* end = name_end;
                unsigned braces = 0; char quote = 0;
                while (*end) {
                    if (quote) { if (*end == quote && end[-1] != '\\') quote = 0; }
                    else if (braces && (*end == '"' || *end == '\'')) quote = *end;
                    else if (*end == '{') braces++;
                    else if (*end == '}' && braces) braces--;
                    else if (*end == '%' && !braces) break;
                    end++;
                }
                if (*end) end++;
                token = wiki_raw(parser, p, end - p, "variable"); p = end;
            }
        }
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::CREOLE && !strncmp(p, "<<<", 3)) {
            const char* end = strstr(p + 3, ">>>");
            if (end) { token = wiki_raw(parser, p, end + 3 - p, "placeholder"); p = end + 3; }
        }
        if (token.item == ITEM_UNDEFINED && !parser->state.link_depth &&
            (!strncmp(p, "http://", 7) || !strncmp(p, "https://", 8) || !strncmp(p, "mailto:", 7))) {
            token = parse_extended_autolink(parser, &p);
        }
        if (token.item == ITEM_UNDEFINED) token = wiki_word(parser, &p, text);
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::XWIKI && !strncmp(p, "image:", 6)) {
            const char* end = p + 6;
            while (*end && !str_is_space(*end)) end++;
            Element* image = create_element(parser, "img");
            add_attribute_to_element(parser, image, "src", wiki_slice(parser, p + 6, end - p - 6));
            token = Item{.element = image}; p = end;
        }
        if (token.item == ITEM_UNDEFINED && syntax->flavor == Flavor::ZIMWIKI && *p == '@' && wiki_name_char(p[1])) {
            const char* end = p + 1;
            while (wiki_name_char(*end)) end++;
            Element* tag = create_element(parser, "span");
            add_attribute_to_element(parser, tag, "data-tag", wiki_slice(parser, p + 1, end - p - 1));
            list_push((List*)tag, wiki_text(parser, p, end - p));
            wiki_record_tag(parser, p + 1, end - p - 1);
            token = Item{.element = tag}; p = end;
        }
        if (token.item == ITEM_UNDEFINED) for (size_t i = 0; i < syntax->emphasis_count; i++) {
            const DelimiterSpec& delimiter = syntax->emphasis[i];
            size_t width = strlen(delimiter.open);
            if (strncmp(p, delimiter.open, width)) continue;
            bool words = (syntax->flavor == Flavor::TWIKI || syntax->flavor == Flavor::VIMWIKI ||
                wiki_is_atlassian(syntax->flavor)) && delimiter.type != InlineType::SUPERSCRIPT &&
                delimiter.type != InlineType::SUBSCRIPT && !(delimiter.type == InlineType::CODE && wiki_is_atlassian(syntax->flavor));
            // Word markup must not consume punctuation inside ordinary compound words.
            if (words && ((p > text && !str_is_space(p[-1]) && !is_ascii_punctuation(p[-1])) ||
                !p[width] || str_is_space(p[width]))) continue;
            if (*p == '/' && p > text && p[-1] == ':') continue;
            const char* end = wiki_find_close(p + width, delimiter.close, syntax->escape);
            size_t close_width = strlen(delimiter.close);
            while (words && end && (end == p + width || str_is_space(end[-1]) ||
                (end[close_width] && !str_is_space(end[close_width]) && !is_ascii_punctuation(end[close_width]))))
                end = wiki_find_close(end + close_width, delimiter.close, syntax->escape);
            if (!end || end == p + width) continue;
            Element* value = create_element(parser, delimiter.type == InlineType::STRIKETHROUGH ? "del" :
                inline_type_to_tag(delimiter.type));
            const char* body = p + width;
            if (syntax->flavor == Flavor::VIMWIKI && delimiter.type == InlineType::BOLD)
                add_attribute_to_element(parser, value, "id", wiki_slice(parser, body, end - body));
            if (delimiter.type == InlineType::CODE) {
                add_attribute_to_element(parser, value, "type", "inline");
                list_push((List*)value, wiki_text(parser, body, end - body));
            } else if (delimiter.type == InlineType::BOLD_ITALIC) {
                Element* inner = create_element(parser, "em");
                list_push((List*)inner, parse_wiki_inlines(parser, wiki_slice(parser, body, end - body)));
                list_push((List*)value, Item{.element = inner});
            } else list_push((List*)value, parse_wiki_inlines(parser, wiki_slice(parser, body, end - body)));
            if (syntax->flavor == Flavor::TWIKI && !strcmp(delimiter.open, "==")) {
                Element* strong = create_element(parser, "strong");
                list_push((List*)strong, Item{.element = value}); value = strong;
            }
            token = Item{.element = value}; p = end + strlen(delimiter.close);
            break;
        }
        if (token.item != ITEM_UNDEFINED && token.item != ITEM_ERROR) {
            if (plain < start) list_push((List*)span, wiki_text(parser, plain, start - plain));
            list_push((List*)span, token); plain = p;
        } else {
            p = start + 1;
        }
        for (const char* cursor = start; cursor < p; cursor++) if (*cursor == '\n') source_line++;
        parser->wiki_inline_line_ = source_line;
    }
    parser->wiki_inline_line_ = saved_line;
    if (plain < p) list_push((List*)span, wiki_text(parser, plain, p - plain));
    parser->wiki_inline_depth_--;
    if (span->length == 1 && get_type_id(span->items[0]) == LMD_TYPE_STRING) return span->items[0];
    return Item{.element = span};
}

} // namespace markup
} // namespace lambda
