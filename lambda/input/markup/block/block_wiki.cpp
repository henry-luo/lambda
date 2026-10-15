// Wiki block parsing shares one tree builder across explicit syntax profiles.
#include "block_common.hpp"
#include "../wiki.hpp"
#include "../../../core/mark_reader.hpp"
#include "lib/str.h"
#include "lib/strbuf.h"

namespace lambda {
namespace markup {

void wiki_record_tag(MarkupParser* parser, const char* tag, size_t length) {
    if (parser->config.flavor == Flavor::VIMWIKI &&
        (parser->wiki_inline_line_ >= 2 || parser->wiki_block_depth_)) return;
    const char* value = wiki_slice(parser, tag, length);
    if (!parser->wiki_tags_) parser->wiki_tags_ = parser->builder.array().final().array;
    for (int64_t i = 0; i < parser->wiki_tags_->length; i++) {
        Item existing = parser->wiki_tags_->items[i];
        if (get_type_id(existing) == LMD_TYPE_STRING && !strcasecmp(existing.get_string()->chars, value)) return;
    }
    array_push(parser->wiki_tags_, wiki_text(parser, tag, length));
    if (!parser->wiki_metadata_) parser->wiki_metadata_ = create_element(parser, "meta");
    parser->builder.putToElement(lam::gc_borrow(parser->wiki_metadata_),
        parser->builder.createName("tags"), Item{.array = parser->wiki_tags_});
}

void wiki_append_fragment(MarkupParser* parser, Element* parent, const char* source,
        size_t length, bool blocks) {
    const char* copy = wiki_slice(parser, source, length);
    if (!blocks) {
        Item contents = parse_inline_spans(parser, copy);
        if (contents.item != ITEM_UNDEFINED) list_push((List*)parent, contents);
        return;
    }
    if (parser->wiki_block_depth_ >= 128) {
        list_push((List*)parent, wiki_raw(parser, source, length, "nesting-limit")); return;
    }
    MarkupParser* nested = markup_parser_create(parser->input(), parser->config);
    if (!nested) return;
    nested->wiki_block_depth_ = parser->wiki_block_depth_ + 1;
    Item doc = nested->parseContent(copy);
    if (get_type_id(doc) == LMD_TYPE_ELEMENT) {
        for (int64_t i = 0; i < doc.element->length; i++) {
            Item child = doc.element->items[i];
            if (get_type_id(child) != LMD_TYPE_ELEMENT) continue;
            ElementReader element(child.element);
            if (strcmp(element.tagName(), "body")) continue;
            for (int64_t j = 0; j < child.element->length; j++) list_push((List*)parent, child.element->items[j]);
        }
    }
    if (nested->wiki_tags_) for (int64_t i = 0; i < nested->wiki_tags_->length; i++) {
        String* tag = nested->wiki_tags_->items[i].get_string();
        if (tag) wiki_record_tag(parser, tag->chars, tag->len);
    }
    markup_parser_destroy(nested);
}

static Item wiki_code_block(MarkupParser* parser, const CodeFenceInfo& fence) {
    StrBuf* body = strbuf_new();
    parser->current_line++;
    bool first = true;
    while (parser->current_line < parser->line_count &&
        !parser->adapter()->isCodeFenceClose(parser->lines[parser->current_line], fence)) {
        if (!first) strbuf_append_char(body, '\n');
        first = false;
        strbuf_append_str(body, parser->lines[parser->current_line++]);
    }
    if (parser->current_line < parser->line_count) parser->current_line++;
    else parser->addWarning("wiki input: unterminated code block");
    Element* code = create_element(parser, "code");
    add_attribute_to_element(parser, code, "type", "block");
    if (fence.info_string && fence.info_length)
        add_attribute_to_element(parser, code, "language", wiki_slice(parser, fence.info_string, fence.info_length));
    list_push((List*)code, wiki_text(parser, body->str, body->length));
    strbuf_free(body);
    return Item{.element = code};
}

static Item wiki_list(MarkupParser* parser) {
    const WikiSyntax* syntax = wiki_syntax(parser->config.flavor);
    Element* lists[MAX_LIST_DEPTH] = {};
    Element* items[MAX_LIST_DEPTH] = {};
    int indents[MAX_LIST_DEPTH] = {};
    bool ordered[MAX_LIST_DEPTH] = {};
    ListItemInfo first = parser->adapter()->detectListItem(parser->lines[parser->current_line]);
    if (syntax->repeated_lists && first.marker_count > MAX_LIST_DEPTH) {
        const char* line = parser->lines[parser->current_line++];
        return wiki_raw(parser, line, strlen(line), "list-depth");
    }
    Element* root = nullptr;
    int current_depth = 0;
    while (parser->current_line < parser->line_count) {
        const char* line = parser->lines[parser->current_line];
        ListItemInfo info = parser->adapter()->detectListItem(line);
        if (!info.valid) {
            if (!is_empty_line(line) && current_depth && count_leading_spaces(line) > first.indent) {
                Item continuation = parse_wiki_block(parser);
                if (continuation.item != ITEM_UNDEFINED) list_push((List*)items[current_depth-1], continuation);
                continue;
            }
            break;
        }
        int depth = syntax->repeated_lists ? (int)info.marker_count : current_depth;
        if (!syntax->repeated_lists) {
            if (!depth) depth = 1;
            else if (info.indent > indents[depth-1]) depth++;
            else while (depth > 1 && info.indent < indents[depth-1]) depth--;
        }
        if (depth > MAX_LIST_DEPTH) {
            list_push((List*)items[current_depth-1], wiki_raw(parser, line, strlen(line), "list-depth"));
            parser->current_line++;
            continue;
        }
        bool root_ordered = syntax->repeated_lists ? info.markers[0] == '#' :
            depth > 1 ? ordered[0] : info.is_ordered;
        if (root && ordered[0] != root_ordered) break;
        for (int level = 0; level < depth; level++) {
            bool kind = syntax->repeated_lists ? info.markers[level] == '#' :
                level == depth - 1 ? info.is_ordered : ordered[level];
            if (level >= current_depth || ordered[level] != kind) {
                Element* list = create_element(parser, kind ? "ol" : "ul");
                if (kind && info.number > 1 && level == depth - 1) {
                    char number[32]; snprintf(number, sizeof(number), "%d", info.number);
                    add_attribute_to_element(parser, list, "start", number);
                }
                if (!level) root = list;
                else {
                    if (!items[level-1]) {
                        items[level-1] = create_element(parser, "li");
                        list_push((List*)lists[level-1], Item{.element = items[level-1]});
                    }
                    list_push((List*)items[level-1], Item{.element = list});
                }
                lists[level] = list; items[level] = nullptr; ordered[level] = kind;
                indents[level] = info.indent;
                current_depth = level + 1;
            }
        }
        current_depth = depth;
        Element* item = create_element(parser, "li");
        items[depth-1] = item;
        if (info.is_task) {
            apply_task_attributes(parser, item, info);
            append_task_checkbox(parser, item, info);
            add_attribute_to_element(parser, lists[depth-1], "class", "contains-task-list");
        }
        parser->wiki_inline_line_ = parser->current_line;
        Item text = parse_inline_spans(parser, info.text_start);
        if (text.item != ITEM_UNDEFINED) list_push((List*)item, text);
        list_push((List*)lists[depth-1], Item{.element = item});
        parser->current_line++;
    }
    return Item{.element = root};
}

const char* wiki_cell_boundary(const char* begin, const char* end, char separator,
        Flavor flavor) {
    unsigned square = 0, curly = 0, parens = 0;
    char escape = wiki_syntax(flavor)->escape;
    char quote = 0;
    for (const char* p = begin; p < end; p++) {
        if (*p == escape && p + 1 < end) { p++; continue; }
        if (quote) { if (*p == quote) quote = 0; continue; }
        if (flavor == Flavor::MEDIAWIKI && (*p == '\'' || *p == '"') && p > begin && p[-1] == '=') {
            quote = *p; continue;
        }
        const char* literal_close = !strncmp(p, "<literal>", 9) ? "</literal>" :
            !strncmp(p, "<nowiki>", 8) ? "</nowiki>" :
            flavor == Flavor::DOKUWIKI && !strncmp(p, "%%", 2) ? "%%" : nullptr;
        if (literal_close) {
            size_t opening = *p == '%' ? 2 : p[1] == 'l' ? 9 : 8;
            const char* closing = strstr(p + opening, literal_close);
            if (closing && closing < end) { p = closing + strlen(literal_close) - 1; continue; }
        }
        if (*p == '[') square++;
        else if (*p == ']' && square) square--;
        else if (*p == '{') curly++;
        else if (*p == '}' && curly) curly--;
        else if (*p == '(') parens++;
        else if (*p == ')' && parens) parens--;
        else if (!square && !curly && !parens && (*p == separator ||
            (flavor == Flavor::DOKUWIKI && (*p == '|' || *p == '^')) ||
            (flavor == Flavor::MEDIAWIKI && *p == '!' && p + 1 < end && p[1] == '!'))) return p;
    }
    return end;
}

static bool separator_row(const char* line) {
    bool dash = false;
    for (const char* p = line; *p; p++) {
        if (*p == '-') dash = true;
        else if (!str_is_space(*p) && *p != '|' && *p != ':' && *p != '+') return false;
    }
    return dash;
}

void wiki_append_table_row(MarkupParser* parser, Element* head, Element* body,
        Element* row, bool header) {
    if (!row) return;
    if (header) for (int64_t i = 0; i < row->length; i++) {
        Item cell = row->items[i];
        if (!strcmp(ElementReader(cell.element).tagName(), "th")) continue;
        Element* th = create_element(parser, "th");
        for (int64_t j = 0; j < cell.element->length; j++) list_push((List*)th, cell.element->items[j]);
        row->items[i] = Item{.element = th};
    }
    list_push((List*)(header && !body->length ? head : body), Item{.element = row});
}

static Item wiki_pipe_table(MarkupParser* parser) {
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    Element* table = create_element(parser, "table");
    Element* head = create_element(parser, "thead");
    Element* body = create_element(parser, "tbody");
    Element* pending = nullptr;
    bool pending_header = false;
    bool tiki = flavor == Flavor::TIKIWIKI;
    bool first = true;
    while (parser->current_line < parser->line_count) {
        const char* line = str_skip_line_space(parser->lines[parser->current_line]);
        if (is_empty_line(line) || (!tiki && !parser->adapter()->detectTable(line, nullptr))) break;
        if (separator_row(line)) {
            wiki_append_table_row(parser, head, body, pending, true);
            pending = nullptr; parser->current_line++; continue;
        }
        wiki_append_table_row(parser, head, body, pending, pending_header);
        Element* row = create_element(parser, "tr");
        const char* end = line + strlen(line);
        bool tiki_end = tiki && end - line >= 2 && !strncmp(end - 2, "||", 2);
        if (tiki_end) end -= 2;
        const char* p = line;
        if (tiki && first) p += 2;
        bool all_header = true;
        while (p < end) {
            char delimiter = *p == '^' ? '^' : '|';
            bool header = *p == '^';
            if (*p == '|' || *p == '^') p++;
            if (wiki_is_atlassian(flavor) && p < end && *p == '|') { header = true; p++; }
            if (p >= end) break;
            if (flavor == Flavor::CREOLE && p < end && *p == '=') { header = true; p++; }
            const char* boundary = wiki_cell_boundary(p, end, delimiter, flavor);
            const char* text = p; size_t length = boundary - p;
            str_trim(&text, &length);
            if (flavor == Flavor::TWIKI && length >= 2 && text[0] == '*' && text[length-1] == '*') {
                header = true; text++; length -= 2;
            }
            Element* cell = create_element(parser, header ? "th" : "td");
            parser->wiki_inline_line_ = parser->current_line;
            Item content = parse_inline_spans(parser, wiki_slice(parser, text, length));
            if (content.item != ITEM_UNDEFINED) list_push((List*)cell, content);
            list_push((List*)row, Item{.element = cell});
            all_header = all_header && header;
            p = boundary;
        }
        pending = row; pending_header = all_header;
        parser->current_line++; first = false;
        if (tiki_end) break;
    }
    wiki_append_table_row(parser, head, body, pending, pending_header);
    if (head->length) list_push((List*)table, Item{.element = head});
    if (body->length) list_push((List*)table, Item{.element = body});
    return Item{.element = table};
}

static Item wiki_definition_list(MarkupParser* parser) {
    Element* list = create_element(parser, "dl");
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    while (parser->current_line < parser->line_count) {
        const char* line = str_skip_line_space(parser->lines[parser->current_line]);
        const char* definition = nullptr;
        const char* term = nullptr;
        size_t term_length = 0;
        if (wiki_is_mediawiki(parser) || flavor == Flavor::TIKIWIKI) {
            if (*line == ';') {
                term = line + 1; definition = strchr(term, ':');
                term_length = definition ? (size_t)(definition - term) : strlen(term);
                if (definition) definition++;
            } else if (*line == ':') definition = line + 1;
            else break;
        } else {
            definition = strstr(line, "::");
            if (!definition) break;
            term = line; term_length = definition - line; definition += 2;
        }
        if (term) {
            Element* dt = create_element(parser, "dt");
            list_push((List*)dt, parse_inline_spans(parser, wiki_slice(parser, term, term_length)));
            list_push((List*)list, Item{.element = dt});
        }
        if (definition) {
            Element* dd = create_element(parser, "dd");
            Item text = parse_inline_spans(parser, str_skip_line_space(definition));
            if (text.item != ITEM_UNDEFINED) list_push((List*)dd, text);
            list_push((List*)list, Item{.element = dd});
        }
        parser->current_line++;
    }
    return Item{.element = list};
}

static bool wiki_definition_start(MarkupParser* parser, const char* line) {
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    if (wiki_is_mediawiki(parser) || flavor == Flavor::TIKIWIKI) return *line == ';' || *line == ':';
    return flavor == Flavor::VIMWIKI && strstr(line, "::") != nullptr;
}

static bool wiki_block_start(MarkupParser* parser, const char* line) {
    FormatAdapter* adapter = parser->adapter();
    WikiMacro macro;
    return adapter->detectHeader(line, nullptr).valid || adapter->detectListItem(line).valid ||
        adapter->detectCodeFence(line).valid || adapter->detectBlockquote(line).valid ||
        adapter->detectTable(line, nullptr) || adapter->detectThematicBreak(line) ||
        wiki_definition_start(parser, str_skip_line_space(line)) ||
        (parser->config.flavor == Flavor::VIMWIKI && *str_skip_line_space(line) == '%') ||
        (parser->config.flavor == Flavor::XWIKI && !strncmp(str_skip_line_space(line), "(%", 2)) ||
        (parser->config.flavor == Flavor::TWIKI && *str_skip_line_space(line) == '#') || wiki_macro_header(parser, str_skip_line_space(line), &macro);
}

static Item wiki_parameters_block(MarkupParser* parser, const char* line, const char* end) {
    parser->current_line++;
    WikiMacro attributes;
    attributes.arguments = line + 2;
    attributes.arguments_length = end - attributes.arguments;
    const char* cursor = attributes.arguments;
    WikiParameter parameter;
    bool supported = true;
    while (wiki_next_parameter(&cursor, end, &parameter)) {
        if (!parameter.key || !((parameter.key_length == 2 && !strncmp(parameter.key, "id", 2)) ||
            (parameter.key_length == 5 && !strncmp(parameter.key, "class", 5)))) supported = false;
    }
    const char* remaining = str_skip_line_space(end + 2);
    Item block = Item{.item = ITEM_UNDEFINED};
    // Parse the same-line form with a child parser so its block syntax stays intact.
    if (*remaining) {
        Element* wrapper = create_element(parser, "div");
        wiki_append_fragment(parser, wrapper, remaining, strlen(remaining), true);
        block = wrapper->length == 1 ? wrapper->items[0] : Item{.element = wrapper};
    } else if (parser->current_line < parser->line_count &&
        !is_empty_line(parser->lines[parser->current_line]) &&
        strncmp(str_skip_line_space(parser->lines[parser->current_line]), "(%", 2)) {
        block = parse_wiki_block(parser);
    }
    if (supported && get_type_id(block) == LMD_TYPE_ELEMENT) {
        const char* id = wiki_macro_parameter(parser, attributes, "id");
        const char* classes = wiki_macro_parameter(parser, attributes, "class");
        if (id) add_attribute_to_element(parser, block.element, "id", id);
        if (classes) add_attribute_to_element(parser, block.element, "class", classes);
        return block;
    }
    Element* wrapper = create_element(parser, "div");
    list_push((List*)wrapper, wiki_raw(parser, line, end + 2 - line, "parameters"));
    if (block.item != ITEM_UNDEFINED) list_push((List*)wrapper, block);
    return Item{.element = wrapper};
}

Item parse_wiki_block(MarkupParser* parser) {
    const char* line = parser->lines[parser->current_line];
    parser->wiki_inline_line_ = parser->current_line;
    const char* p = str_skip_line_space(line);
    FormatAdapter* adapter = parser->adapter();
    Flavor flavor = wiki_syntax(parser->config.flavor)->flavor;
    if (flavor == Flavor::ZIMWIKI && parser->wiki_header_ &&
        (!strncmp(p, "Content-Type: text/x-zim-wiki", strlen("Content-Type: text/x-zim-wiki")) || !strncmp(p, "Wiki-Format:", 12) || !strncmp(p, "Creation-Date:", 14))) {
        if (!parser->wiki_metadata_) parser->wiki_metadata_ = create_element(parser, "meta");
        if (!strncmp(p, "Creation-Date:", 14)) add_attribute_to_element(parser, parser->wiki_metadata_, "created", str_skip_line_space(p + 14));
        parser->current_line++; return Item{.item = ITEM_UNDEFINED};
    }
    parser->wiki_header_ = false;
    if (flavor == Flavor::VIMWIKI && *p == '%') {
        parser->current_line++;
        Item directive = wiki_raw(parser, line, strlen(line), "directive");
        add_attribute_to_element(parser, directive.element, "type", "block");
        return directive;
    }
    if (flavor == Flavor::XWIKI && !strncmp(p, "(%", 2)) {
        const char* end = strstr(p + 2, "%)");
        if (end) return wiki_parameters_block(parser, p, end);
    }
    if (flavor == Flavor::TWIKI && *p == '#' && p[1] && !strchr(p, ' ')) {
        Element* anchor = create_element(parser, "span");
        add_attribute_to_element(parser, anchor, "id", p + 1);
        parser->current_line++; return Item{.element = anchor};
    }
    if (adapter->detectHeader(line, nullptr).valid) return parse_header(parser, line);
    CodeFenceInfo fence = adapter->detectCodeFence(line);
    if (fence.valid) return wiki_code_block(parser, fence);
    if (adapter->detectListItem(line).valid) return wiki_list(parser);
    if (wiki_definition_start(parser, p)) return wiki_definition_list(parser);
    if (adapter->detectTable(line, nullptr))
        return wiki_is_mediawiki(parser) ? parse_mediawiki_table(parser, line) : wiki_pipe_table(parser);
    if (adapter->detectThematicBreak(line)) return parse_divider(parser);
    BlockquoteInfo quote = adapter->detectBlockquote(line);
    if (quote.valid) {
        Element* block = create_element(parser, "blockquote");
        StrBuf* contents = strbuf_new();
        while (parser->current_line < parser->line_count) {
            quote = adapter->detectBlockquote(parser->lines[parser->current_line]);
            if (!quote.valid) break;
            if (contents->length) strbuf_append_char(contents, '\n');
            strbuf_append_str(contents, quote.content_start); parser->current_line++;
        }
        wiki_append_fragment(parser, block, contents->str, contents->length, true);
        strbuf_free(contents); return Item{.element = block};
    }
    WikiMacro macro;
    if (wiki_macro_header(parser, p, &macro)) {
        StrBuf* source = strbuf_new();
        for (int i = parser->current_line; i < parser->line_count; i++) {
            if (i != parser->current_line) strbuf_append_char(source, '\n');
            strbuf_append_str(source, parser->lines[i]);
        }
        WikiMacro complete;
        wiki_macro_header(parser, str_skip_line_space(source->str), &complete);
        const char* close = wiki_macro_body_end(parser, complete);
        const char* end = close ? close + strlen(complete.close) : strchr(source->str, '\n');
        if (!end) end = source->str + source->length;
        while (*end && *end != '\n') end++;
        int end_line = parser->current_line;
        for (const char* cursor = source->str; cursor < end; cursor++) if (*cursor == '\n') end_line++;
        source->length = end - source->str; source->str[source->length] = 0;
        parser->current_line = end_line + 1;
        const char* cursor = str_skip_line_space(source->str);
        Item block = parse_wiki_macro(parser, &cursor, true);
        if (get_type_id(block) == LMD_TYPE_ELEMENT && !strcmp(ElementReader(block.element).tagName(), "raw"))
            add_attribute_to_element(parser, block.element, "type", "block");
        if (*str_skip_line_space(cursor)) {
            Element* wrapper = create_element(parser, "div");
            list_push((List*)wrapper, block);
            list_push((List*)wrapper, parse_inline_spans(parser, cursor));
            block = Item{.element = wrapper};
        }
        strbuf_free(source); return block;
    }
    StrBuf* text = strbuf_new();
    while (parser->current_line < parser->line_count) {
        const char* next = parser->lines[parser->current_line];
        if (is_empty_line(next) || (text->length && wiki_block_start(parser, next))) break;
        if (text->length) strbuf_append_char(text, (flavor == Flavor::CONFLUENCE || flavor == Flavor::ZIMWIKI || flavor == Flavor::VIMWIKI) ? '\n' : ' ');
        strbuf_append_str(text, str_skip_line_space(next)); parser->current_line++;
    }
    Element* paragraph = create_element(parser, "p");
    Item content = parse_inline_spans(parser, text->str);
    if (content.item != ITEM_UNDEFINED) list_push((List*)paragraph, content);
    strbuf_free(text); return Item{.element = paragraph};
}

} // namespace markup
} // namespace lambda
