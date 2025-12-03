// latex_commands.cpp - Command parsing and dispatch
// Registry-based command handling with argument parsing

#include "latex_parser.hpp"
#include "latex_registry.hpp"
#include "../input.hpp"
#include "../../../lib/log.h"
#include <cstring>

namespace lambda {
namespace latex {

// Forward declaration for math parser
extern "C" void parse_math(Input* input, const char* math_string, const char* flavor);

// =============================================================================
// Main Command Entry Point
// =============================================================================

Item LatexParser::parse_command() {
    if (peek() != '\\') {
        return ItemNull;
    }

    size_t start_offset = offset();
    advance();  // skip backslash

    // try control space first (\ followed by space/newline)
    if (peek() == ' ' || peek() == '\n' || peek() == '\t') {
        advance();
        return create_text("\u200B ", 4);  // ZWSP + space
    }

    // try control symbols
    if (Item r = parse_ctrl_sym_after_backslash(); r.item != ITEM_NULL) {
        return r;
    }

    // try diacritic
    if (Item r = parse_diacritic_after_backslash(); r.item != ITEM_NULL) {
        return r;
    }

    // try character code notation
    if (Item r = parse_charsym_after_backslash(); r.item != ITEM_NULL) {
        return r;
    }

    // parse command name
    std::string name = parse_command_name();
    if (name.empty()) {
        return ItemError;
    }

    // dispatch based on command name
    return dispatch_command(name);
}

// =============================================================================
// Control Symbol Handling (after backslash already consumed)
// =============================================================================

Item LatexParser::parse_ctrl_sym_after_backslash() {
    char c = peek();

    // escaped special characters
    if (strchr("$%#&{}_ ", c)) {
        advance();
        if (c == ' ') {
            return create_text("\u200B ", 4);  // ZWSP + space
        }
        return create_text(&c, 1);
    }

    // thin space
    if (c == ',') {
        advance();
        return create_element("thinspace");
    }

    // soft hyphen
    if (c == '-') {
        advance();
        return create_text("\u00AD", 2);
    }

    // italic correction (ZWNJ)
    if (c == '/') {
        advance();
        return create_text("\u200C", 3);
    }

    // end-of-sentence marker
    if (c == '@') {
        advance();
        return create_text("\u200B", 3);
    }

    // line break
    if (c == '\\') {
        advance();
        return parse_linebreak_args();
    }

    return ItemNull;
}

// =============================================================================
// Diacritic Handling (after backslash already consumed)
// =============================================================================

Item LatexParser::parse_diacritic_after_backslash() {
    char c = peek();
    const DiacriticInfo* diac = find_diacritic(c);
    if (!diac) return ItemNull;

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
                    stringbuf_append_char(sb, peek());
                    has_base = true;
                    advance();
                }
            } else {
                append_utf8_char(sb);
                has_base = true;
            }
        }

        // skip to closing brace
        while (!at_end() && peek() != '}') advance();
        if (peek() == '}') advance();

    } else if (peek() == '\\' && remaining() > 1 && (pos_[1] == 'i' || pos_[1] == 'j')) {
        advance();  // skip backslash
        if (peek() == 'i') {
            stringbuf_append_str(sb, "\u0131");
        } else {
            stringbuf_append_str(sb, "\u0237");
        }
        has_base = true;
        advance();
        if (peek() == ' ') advance();  // gobble space

    } else if (!at_end() && peek() != ' ' && peek() != '\n' && peek() != '\t' &&
               peek() != '\\' && peek() != '{' && peek() != '}') {
        append_utf8_char(sb);
        has_base = true;
    }

    if (has_base) {
        stringbuf_append_str(sb, diac->combining);
    } else {
        stringbuf_append_str(sb, diac->standalone);
        stringbuf_append_str(sb, "\u200B");
    }

    return create_text(sb->str->chars, sb->length);
}

// =============================================================================
// Character Code Notation (after backslash already consumed)
// =============================================================================

Item LatexParser::parse_charsym_after_backslash() {
    // \symbol{num}
    if (lookahead("symbol")) {
        match("symbol");
        expect('{');
        int code = parse_integer();
        expect('}');
        return char_from_code(code);
    }

    // \char
    if (lookahead("char")) {
        match("char");

        if (match('\'')) {
            int code = parse_octal();
            return char_from_code(code);
        }
        if (match('"')) {
            int code = parse_hex(2);
            return char_from_code(code);
        }
        int code = parse_integer();
        return char_from_code(code);
    }

    return ItemNull;
}

// =============================================================================
// Command Name Parsing
// =============================================================================

std::string LatexParser::parse_command_name() {
    std::string name;

    // single-character control symbols
    if (!at_end() && strchr("$%#&{}_\\-,/@^~'`\"=.", peek())) {
        name += advance();
        return name;
    }

    // alphabetic command names (can include *)
    while (!at_end() && (isalpha(peek()) || (peek() == '*' && !name.empty()))) {
        name += advance();
    }

    return name;
}

// =============================================================================
// Command Dispatch
// =============================================================================

Item LatexParser::dispatch_command(const std::string& name) {
    // look up in registry
    const CommandSpec* spec = find_command(name.c_str());

    if (spec) {
        switch (spec->handler) {
            case CommandSpec::Handler::Symbol:
                return parse_symbol_command(name);

            case CommandSpec::Handler::Font:
                return parse_font_command(name);

            case CommandSpec::Handler::Spacing:
                return parse_spacing_command(name);

            case CommandSpec::Handler::Section:
                return parse_section_command(name, get_section_level(name));

            case CommandSpec::Handler::Counter:
                return parse_counter_command(name);

            case CommandSpec::Handler::Ref:
                return parse_ref_command(name);

            case CommandSpec::Handler::Environment:
                return parse_environment();

            case CommandSpec::Handler::Verb:
                return parse_verb_command();

            case CommandSpec::Handler::Item:
                return parse_item_command();

            case CommandSpec::Handler::Special:
            case CommandSpec::Handler::Default:
                return parse_generic_command(name, spec);
        }
    }

    // handle common commands not in registry or fallback

    // begin/end environments
    if (name == "begin") {
        return parse_begin_env();
    }
    if (name == "end") {
        // skip end - usually handled by environment parser
        skip_spaces();
        if (match('{')) {
            parse_identifier();
            match('}');
        }
        return ItemNull;
    }

    // line break commands
    if (name == "newline" || name == "linebreak") {
        return parse_linebreak_args();
    }
    if (name == "par") {
        return create_element("par");
    }

    // gobble trailing space for alphabetic commands
    if (!name.empty() && isalpha(name[0]) && peek() == ' ') {
        advance();
    }

    // create generic command element
    return parse_generic_command(name, nullptr);
}

// =============================================================================
// Generic Command Parsing
// =============================================================================

Item LatexParser::parse_generic_command(const std::string& name, const CommandSpec* spec) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // gobble trailing space for alphabetic commands
    if (!name.empty() && isalpha(name[0])) {
        if (peek() == ' ' || peek() == '\t') {
            advance();
        }
    }

    // parse arguments based on spec
    if (spec && spec->arg_spec && spec->arg_spec[0]) {
        std::vector<Item> args = parse_command_args(spec->arg_spec);
        for (const Item& arg : args) {
            if (arg.item != ITEM_NULL && arg.item != ITEM_ERROR) {
                list_push((List*)elem, arg);
            }
        }
    } else {
        // default argument parsing: optional [] and required {}
        skip_spaces();

        // optional arguments [...]
        while (peek() == '[') {
            advance();
            Item opt = parse_balanced_content(']');
            if (opt.item != ITEM_NULL && opt.item != ITEM_ERROR) {
                list_push((List*)elem, opt);
            }
            skip_spaces();
        }

        // required arguments {...}
        while (peek() == '{') {
            advance();

            // parse content as LaTeX, not raw text
            Element* arg_elem = builder_.element("argument").final().element;
            if (arg_elem) {
                while (!at_end() && peek() != '}') {
                    Item child = parse_content();
                    if (child.item == ITEM_ERROR) break;
                    if (child.item != ITEM_NULL) {
                        list_push((List*)arg_elem, child);
                    }
                }
                match('}');

                ((TypeElmt*)arg_elem->type)->content_length = ((List*)arg_elem)->length;
                list_push((List*)elem, Item{.element = arg_elem});
            }

            skip_spaces();
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Argument Specification Parsing
// =============================================================================

std::vector<ArgSpec> LatexParser::parse_arg_spec(const char* spec) {
    std::vector<ArgSpec> args;

    while (*spec) {
        ArgSpec arg;
        arg.type = (ArgType)*spec++;
        arg.optional = (*spec == '?');
        if (arg.optional) spec++;
        args.push_back(arg);
        if (*spec == ' ') spec++;  // skip separator
    }

    return args;
}

std::vector<Item> LatexParser::parse_command_args(const char* spec) {
    std::vector<Item> result;
    auto args = parse_arg_spec(spec);

    for (const auto& arg : args) {
        Item parsed = parse_single_arg(arg.type, arg.optional);
        result.push_back(parsed);
    }

    return result;
}

Item LatexParser::parse_single_arg(ArgType type, bool optional) {
    skip_spaces();

    switch (type) {
        case ArgType::Star:
            // check for *
            if (match('*')) {
                return builder_.createBool(true);
            }
            return builder_.createBool(false);

        case ArgType::Group:
            // required group {...}
            if (peek() == '{') {
                advance();
                Element* arg = builder_.element("argument").final().element;
                if (arg) {
                    while (!at_end() && peek() != '}') {
                        Item child = parse_content();
                        if (child.item == ITEM_ERROR) break;
                        if (child.item != ITEM_NULL) {
                            list_push((List*)arg, child);
                        }
                    }
                    match('}');
                    ((TypeElmt*)arg->type)->content_length = ((List*)arg)->length;
                    return Item{.element = arg};
                }
            } else if (!optional) {
                error("Expected '{'");
            }
            return ItemNull;

        case ArgType::OptGroup:
            // optional group [...]
            if (peek() == '[') {
                advance();
                return parse_balanced_content(']');
            }
            return ItemNull;

        case ArgType::Identifier:
            // identifier {name}
            if (peek() == '{') {
                advance();
                skip_spaces();
                std::string id = parse_identifier();
                skip_spaces();
                match('}');
                return create_text(id);
            } else if (!optional) {
                error("Expected '{identifier}'");
            }
            return ItemNull;

        case ArgType::Number:
            // number expression {num}
            if (peek() == '{') {
                advance();
                int val = parse_num_expr();
                match('}');
                return builder_.createInt(val);
            } else if (!optional) {
                error("Expected '{number}'");
            }
            return ItemNull;

        case ArgType::Length:
            // length {12pt}
            if (peek() == '{') {
                advance();
                Length len = parse_length();
                match('}');
                // return as string for now
                char buf[64];
                snprintf(buf, sizeof(buf), "%g%s", len.value, len.unit.c_str());
                return create_text(buf);
            } else if (!optional) {
                error("Expected '{length}'");
            }
            return ItemNull;

        default:
            return ItemNull;
    }
}

// =============================================================================
// Symbol Commands
// =============================================================================

Item LatexParser::parse_symbol_command(const std::string& name) {
    // gobble trailing space for alphabetic commands
    if (!name.empty() && isalpha(name[0])) {
        if (peek() == ' ' || peek() == '\t') {
            advance();
        }
    }

    // check for Unicode mapping
    const char* unicode = symbol_to_unicode(name.c_str());
    if (unicode) {
        return create_text(unicode, strlen(unicode));
    }

    // create element for unrecognized symbols
    return create_element(name.c_str());
}

// =============================================================================
// Font Commands
// =============================================================================

Item LatexParser::parse_font_command(const std::string& name) {
    const CommandSpec* spec = find_command(name.c_str());

    // font declarations (no argument)
    if (spec && spec->is_symbol) {
        if (peek() == ' ' || peek() == '\t') {
            advance();  // gobble space
        }
        return create_element(name.c_str());
    }

    // font commands with argument
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    skip_spaces();

    if (peek() == '{') {
        advance();

        // parse content
        while (!at_end() && peek() != '}') {
            Item child = parse_content();
            if (child.item == ITEM_ERROR) break;
            if (child.item != ITEM_NULL) {
                list_push((List*)elem, child);
            }
        }
        match('}');
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Spacing Commands
// =============================================================================

Item LatexParser::parse_spacing_command(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    const CommandSpec* spec = find_command(name.c_str());

    // simple spacing commands (no arguments)
    if (!spec || !spec->arg_spec || !spec->arg_spec[0]) {
        return Item{.element = elem};
    }

    // parse arguments
    std::vector<Item> args = parse_command_args(spec->arg_spec);
    for (const Item& arg : args) {
        if (arg.item != ITEM_NULL && arg.item != ITEM_ERROR) {
            list_push((List*)elem, arg);
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// Line break with optional spacing argument
Item LatexParser::parse_linebreak_args() {
    Element* elem = builder_.element("linebreak").final().element;
    if (!elem) {
        return ItemError;
    }

    skip_whitespace();

    // optional spacing argument [...]
    if (peek() == '[') {
        advance();
        StringBuf* sb = ctx_.sb;
        stringbuf_reset(sb);

        while (!at_end() && peek() != ']') {
            stringbuf_append_char(sb, advance());
        }
        match(']');

        if (sb->length > 0) {
            Item dim = create_text(sb->str->chars, sb->length);
            if (dim.item != ITEM_NULL && dim.item != ITEM_ERROR) {
                list_push((List*)elem, dim);
            }
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Counter Commands
// =============================================================================

Item LatexParser::parse_counter_command(const std::string& name) {
    // counter commands are already well implemented in existing parser
    // this is a basic implementation that creates elements

    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    const CommandSpec* spec = find_command(name.c_str());
    if (spec && spec->arg_spec && spec->arg_spec[0]) {
        std::vector<Item> args = parse_command_args(spec->arg_spec);
        for (const Item& arg : args) {
            if (arg.item != ITEM_NULL && arg.item != ITEM_ERROR) {
                list_push((List*)elem, arg);
            }
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Reference Commands
// =============================================================================

Item LatexParser::parse_ref_command(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    const CommandSpec* spec = find_command(name.c_str());
    if (spec && spec->arg_spec && spec->arg_spec[0]) {
        std::vector<Item> args = parse_command_args(spec->arg_spec);
        for (const Item& arg : args) {
            if (arg.item != ITEM_NULL && arg.item != ITEM_ERROR) {
                list_push((List*)elem, arg);
            }
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Verb Command
// =============================================================================

Item LatexParser::parse_verb_command() {
    // already past \verb
    bool show_spaces = false;

    // check for verb*
    if (peek() == '*') {
        advance();
        show_spaces = true;
    }

    // get delimiter
    if (at_end()) {
        error("Expected delimiter after \\verb");
        return ItemError;
    }

    char delimiter = advance();

    // collect content until closing delimiter
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    while (!at_end() && peek() != delimiter) {
        if (show_spaces && peek() == ' ') {
            stringbuf_append_str(sb, "\xE2\x90\xA3");  // U+2423 OPEN BOX
        } else {
            stringbuf_append_char(sb, peek());
        }
        advance();
    }

    if (peek() == delimiter) {
        advance();  // skip closing delimiter
    }

    // create verb element
    Element* elem = builder_.element("verb").final().element;
    if (elem && sb->length > 0) {
        Item text = create_text(sb->str->chars, sb->length);
        if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
            list_push((List*)elem, text);
        }
        ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
        return Item{.element = elem};
    }

    return ItemError;
}

// =============================================================================
// Item Command (for lists)
// =============================================================================

Item LatexParser::parse_item_command() {
    Element* elem = builder_.element("item").final().element;
    if (!elem) {
        return ItemError;
    }

    skip_whitespace();

    // optional label [...]
    if (peek() == '[') {
        advance();

        Element* label = builder_.element("label").final().element;
        if (label) {
            int bracket_depth = 1;

            while (!at_end() && bracket_depth > 0) {
                if (peek() == '[') {
                    bracket_depth++;
                    Item text = create_text("[", 1);
                    list_push((List*)label, text);
                    advance();
                } else if (peek() == ']') {
                    bracket_depth--;
                    if (bracket_depth > 0) {
                        Item text = create_text("]", 1);
                        list_push((List*)label, text);
                        advance();
                    } else {
                        advance();  // skip final ]
                    }
                } else if (peek() == '\\') {
                    Item child = parse_command();
                    if (child.item != ITEM_ERROR && child.item != ITEM_NULL) {
                        list_push((List*)label, child);
                    }
                } else if (peek() == '{') {
                    Item child = parse_group();
                    if (child.item != ITEM_ERROR && child.item != ITEM_NULL) {
                        list_push((List*)label, child);
                    }
                } else {
                    // collect text
                    const char* start = pos_;
                    while (!at_end() && peek() != '[' && peek() != ']' &&
                           peek() != '\\' && peek() != '{') {
                        advance();
                    }
                    if (pos_ > start) {
                        Item text = create_text(start, pos_ - start);
                        list_push((List*)label, text);
                    }
                }
            }

            ((TypeElmt*)label->type)->content_length = ((List*)label)->length;
            list_push((List*)elem, Item{.element = label});
        }

        skip_whitespace();
    }

    // parse item content until next \item or \end
    while (!at_end()) {
        skip_whitespace();
        if (at_end()) break;

        // check for \item or \end
        if (lookahead("\\item") || lookahead("\\end{")) {
            break;
        }

        Item child = parse_content();
        if (child.item == ITEM_ERROR) break;
        if (child.item != ITEM_NULL) {
            list_push((List*)elem, child);
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Section Commands
// =============================================================================

Item LatexParser::parse_section_command(const std::string& name, int level) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // check for star
    bool starred = match('*');
    if (starred) {
        builder_.putToElement(elem, builder_.createName("starred"),
                              builder_.createBool(true));
    }

    skip_spaces();

    // optional toc entry [...]
    if (peek() == '[') {
        advance();
        Item toc = parse_balanced_content(']');
        if (toc.item != ITEM_NULL && toc.item != ITEM_ERROR) {
            // wrap in toc element
            Element* toc_elem = builder_.element("toc").final().element;
            if (toc_elem) {
                list_push((List*)toc_elem, toc);
                ((TypeElmt*)toc_elem->type)->content_length = 1;
                list_push((List*)elem, Item{.element = toc_elem});
            }
        }
        skip_spaces();
    }

    // required title {...}
    if (peek() == '{') {
        advance();

        Element* title = builder_.element("title").final().element;
        if (title) {
            while (!at_end() && peek() != '}') {
                Item child = parse_content();
                if (child.item == ITEM_ERROR) break;
                if (child.item != ITEM_NULL) {
                    list_push((List*)title, child);
                }
            }
            match('}');

            ((TypeElmt*)title->type)->content_length = ((List*)title)->length;
            list_push((List*)elem, Item{.element = title});
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Math Mode
// =============================================================================

Item LatexParser::parse_inline_math() {
    // $...$
    if (!match('$')) return ItemNull;

    // check for display math $$...$$
    if (match('$')) {
        return parse_display_math_content();
    }

    return parse_math_content_impl("$", false);
}

Item LatexParser::parse_display_math() {
    // $$...$$ or \[...\]
    if (match("$$")) {
        return parse_display_math_content();
    }
    if (match("\\[")) {
        return parse_math_content_impl("\\]", true);
    }
    return ItemNull;
}

Item LatexParser::parse_display_math_content() {
    return parse_math_content_impl("$$", true);
}

Item LatexParser::parse_math_content_impl(const std::string& delimiter, bool display) {
    // collect math content
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    size_t delim_len = delimiter.length();

    while (!at_end()) {
        // check for closing delimiter
        if (remaining() >= delim_len && strncmp(pos_, delimiter.c_str(), delim_len) == 0) {
            pos_ += delim_len;
            break;
        }

        stringbuf_append_char(sb, advance());
    }

    if (sb->length == 0) {
        return ItemError;
    }

    // create wrapper element
    const char* elem_name = display ? "displaymath" : "math";
    Element* elem = builder_.element(elem_name).final().element;
    if (!elem) {
        return ItemError;
    }

    // delegate to math parser
    Input* math_input = InputManager::create_input((Url*)input_->url);
    if (math_input) {
        stringbuf_reset(ctx_.sb);  // reset before calling

        parse_math(math_input, sb->str->chars, "latex");

        stringbuf_reset(ctx_.sb);  // reset after calling

        if (math_input->root.item != ITEM_NULL) {
            list_push((List*)elem, math_input->root);
        }

        // cleanup
        if (math_input->type_list) {
            arraylist_free(math_input->type_list);
        }
        pool_destroy(math_input->pool);
        free(math_input);
    } else {
        // fallback: just store raw text
        Item text = create_text(sb->str->chars, sb->length);
        if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
            list_push((List*)elem, text);
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

Item LatexParser::parse_math_content(const std::string& delimiter) {
    return parse_math_content_impl(delimiter, false);
}

} // namespace latex
} // namespace lambda
