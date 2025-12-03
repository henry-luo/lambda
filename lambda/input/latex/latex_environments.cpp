// latex_environments.cpp - Environment handling
// Implements \begin{...} \end{...} parsing for various environment types

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
// Main Environment Entry Point
// =============================================================================

Item LatexParser::parse_environment() {
    // called when we've seen \begin or dispatcher routes here
    return parse_begin_env();
}

Item LatexParser::parse_begin_env() {
    // parse \begin{envname}
    // at this point we're either after \begin or we need to match it

    if (!lookahead("{")) {
        // we need the environment name in braces
        skip_spaces();
        if (!match('{')) {
            error("Expected '{' after \\begin");
            return ItemError;
        }
    } else {
        match('{');
    }

    // get environment name
    skip_spaces();
    std::string name = parse_identifier();

    // check for starred variant
    bool starred = false;
    if (peek() == '*') {
        starred = true;
        name += '*';
        advance();
    }

    skip_spaces();
    if (!match('}')) {
        error("Expected '}' after environment name");
        return ItemError;
    }

    // look up environment spec
    const EnvironmentSpec* spec = find_environment(name.c_str());

    // dispatch based on environment type
    if (spec) {
        switch (spec->type) {
            case EnvType::Math:
                return parse_math_environment_content(name);

            case EnvType::Verbatim:
                return parse_verbatim_environment(name);

            case EnvType::List:
                return parse_list_environment(name);

            case EnvType::Tabular:
                return parse_tabular_environment(name);

            case EnvType::Figure:
            case EnvType::Theorem:
            case EnvType::Generic:
            default:
                return parse_generic_environment(name);
        }
    }

    // unknown environment - treat as generic
    return parse_generic_environment(name);
}

Item LatexParser::parse_end_env(const std::string& expected_name) {
    // parse \end{envname} and verify match
    skip_spaces();

    if (!match('{')) {
        error("Expected '{' after \\end");
        return ItemError;
    }

    skip_spaces();
    std::string name = parse_identifier();

    if (peek() == '*') {
        name += '*';
        advance();
    }

    skip_spaces();
    if (!match('}')) {
        error("Expected '}' after environment name");
        return ItemError;
    }

    if (name != expected_name) {
        error("Mismatched environment: expected \\end{%s}, got \\end{%s}",
              expected_name.c_str(), name.c_str());
        return ItemError;
    }

    return ItemNull;  // success
}

// =============================================================================
// Generic Environment
// =============================================================================

Item LatexParser::parse_generic_environment(const std::string& name) {
    // create element with environment name as tag
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // parse optional arguments/options after environment name
    skip_spaces();

    // optional argument [...]
    if (peek() == '[') {
        advance();
        Item opt = parse_balanced_content(']');
        if (opt.item != ITEM_NULL && opt.item != ITEM_ERROR) {
            // wrap in options element
            Element* opt_elem = builder_.element("options").final().element;
            if (opt_elem) {
                list_push((List*)opt_elem, opt);
                ((TypeElmt*)opt_elem->type)->content_length = 1;
                builder_.putToElement(elem, builder_.createName("options"),
                                      Item{.element = opt_elem});
            }
        }
        skip_spaces();
    }

    // required argument {...} for some environments
    const EnvironmentSpec* spec = find_environment(name.c_str());
    if (spec && spec->arg_spec && spec->arg_spec[0]) {
        std::vector<Item> args = parse_command_args(spec->arg_spec);
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i].item != ITEM_NULL && args[i].item != ITEM_ERROR) {
                list_push((List*)elem, args[i]);
            }
        }
    }

    // parse content until \end{name}
    skip_whitespace();

    while (!at_end()) {
        // check for \end{name}
        if (lookahead("\\end{")) {
            const char* check = pos_ + 5;  // skip "\end{"
            size_t name_len = name.length();
            if (check + name_len < end_ &&
                strncmp(check, name.c_str(), name_len) == 0 &&
                check[name_len] == '}') {
                // found matching \end
                pos_ = check + name_len + 1;
                break;
            }
        }

        // parse content
        if (peek() == '\\') {
            Item child = parse_command();
            if (child.item == ITEM_ERROR) break;
            if (child.item != ITEM_NULL) {
                list_push((List*)elem, child);
            }
        } else if (peek() == '%') {
            skip_comment();
        } else {
            Item text = parse_text();
            if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
                list_push((List*)elem, text);
            }
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// List Environment (itemize, enumerate, description)
// =============================================================================

Item LatexParser::parse_list_environment(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // parse optional arguments
    skip_spaces();
    if (peek() == '[') {
        advance();
        Item opt = parse_balanced_content(']');
        if (opt.item != ITEM_NULL && opt.item != ITEM_ERROR) {
            Element* opt_elem = builder_.element("options").final().element;
            if (opt_elem) {
                list_push((List*)opt_elem, opt);
                ((TypeElmt*)opt_elem->type)->content_length = 1;
                builder_.putToElement(elem, builder_.createName("options"),
                                      Item{.element = opt_elem});
            }
        }
        skip_spaces();
    }

    // skip whitespace before items
    skip_whitespace();

    // parse items until \end{name}
    while (!at_end()) {
        // check for \end{name}
        if (lookahead("\\end{")) {
            const char* check = pos_ + 5;
            size_t name_len = name.length();
            if (check + name_len < end_ &&
                strncmp(check, name.c_str(), name_len) == 0 &&
                check[name_len] == '}') {
                pos_ = check + name_len + 1;
                break;
            }
        }

        // expect \item commands
        skip_whitespace();

        if (lookahead("\\item")) {
            match("\\item");
            Item item = parse_item_command();
            if (item.item != ITEM_NULL && item.item != ITEM_ERROR) {
                list_push((List*)elem, item);
            }
        } else if (lookahead("\\end{")) {
            // end of environment
            continue;
        } else if (!at_end()) {
            // skip unexpected content
            Item child = parse_content();
            if (child.item == ITEM_ERROR) break;
            // ignore non-item content
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Tabular Environment
// =============================================================================

Item LatexParser::parse_tabular_environment(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // parse column specification
    skip_spaces();

    // optional position argument for tabular* etc
    if (name == "tabular*" || name == "tabularx") {
        if (peek() == '{') {
            advance();
            Item width = parse_balanced_content('}');
            if (width.item != ITEM_NULL && width.item != ITEM_ERROR) {
                builder_.putToElement(elem, builder_.createName("width"), width);
            }
        }
        skip_spaces();
    }

    // optional position [t/b/c]
    if (peek() == '[') {
        advance();
        Item pos = parse_balanced_content(']');
        if (pos.item != ITEM_NULL && pos.item != ITEM_ERROR) {
            builder_.putToElement(elem, builder_.createName("position"), pos);
        }
        skip_spaces();
    }

    // column spec {lcr...}
    if (peek() == '{') {
        advance();
        StringBuf* sb = ctx_.sb;
        stringbuf_reset(sb);

        int depth = 1;
        while (!at_end() && depth > 0) {
            char c = peek();
            if (c == '{') depth++;
            else if (c == '}') depth--;

            if (depth > 0) {
                stringbuf_append_char(sb, c);
            }
            advance();
        }

        if (sb->length > 0) {
            Item colspec = create_text(sb->str->chars, sb->length);
            builder_.putToElement(elem, builder_.createName("colspec"), colspec);
        }
        skip_spaces();
    }

    // parse table content until \end{name}
    skip_whitespace();

    // create rows element
    Element* tbody = builder_.element("tbody").final().element;
    Element* current_row = nullptr;

    while (!at_end()) {
        // check for \end{name}
        if (lookahead("\\end{")) {
            const char* check = pos_ + 5;
            size_t name_len = name.length();
            if (check + name_len < end_ &&
                strncmp(check, name.c_str(), name_len) == 0 &&
                check[name_len] == '}') {
                pos_ = check + name_len + 1;
                break;
            }
        }

        // start new row if needed
        if (!current_row) {
            current_row = builder_.element("tr").final().element;
        }

        // check for row separator (double backslash)
        if (lookahead("\\\\")) {
            match("\\\\");

            // optional [length] after row separator
            skip_spaces();
            if (peek() == '[') {
                advance();
                parse_balanced_content(']');
            }

            // finish current row
            if (current_row && tbody) {
                ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
                list_push((List*)tbody, Item{.element = current_row});
            }
            current_row = nullptr;
            skip_whitespace();
            continue;
        }

        // check for \hline, \cline
        if (lookahead("\\hline") || lookahead("\\cline")) {
            Item cmd = parse_command();
            // hlines are attributes on rows, but for simplicity we'll skip them
            (void)cmd;
            continue;
        }

        // parse cell content
        Element* cell = builder_.element("td").final().element;

        while (!at_end()) {
            // check for cell separator &
            if (peek() == '&') {
                advance();
                break;
            }

            // check for row end
            if (lookahead("\\\\") || lookahead("\\end{")) {
                break;
            }

            // parse cell content
            Item content = parse_content();
            if (content.item == ITEM_ERROR) break;
            if (content.item != ITEM_NULL && cell) {
                list_push((List*)cell, content);
            }
        }

        // add cell to row
        if (cell && current_row) {
            ((TypeElmt*)cell->type)->content_length = ((List*)cell)->length;
            list_push((List*)current_row, Item{.element = cell});
        }
    }

    // add last row if not empty
    if (current_row && tbody) {
        if (((List*)current_row)->length > 0) {
            ((TypeElmt*)current_row->type)->content_length = ((List*)current_row)->length;
            list_push((List*)tbody, Item{.element = current_row});
        }
    }

    // add tbody to table
    if (tbody) {
        ((TypeElmt*)tbody->type)->content_length = ((List*)tbody)->length;
        list_push((List*)elem, Item{.element = tbody});
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Verbatim Environment
// =============================================================================

Item LatexParser::parse_verbatim_environment(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // parse optional arguments for lstlisting, minted, etc
    skip_spaces();
    if (peek() == '[') {
        advance();
        Item opt = parse_balanced_content(']');
        if (opt.item != ITEM_NULL && opt.item != ITEM_ERROR) {
            builder_.putToElement(elem, builder_.createName("options"), opt);
        }
        skip_spaces();
    }

    // for minted: parse {language}
    if (name == "minted") {
        if (peek() == '{') {
            advance();
            Item lang = parse_balanced_content('}');
            if (lang.item != ITEM_NULL && lang.item != ITEM_ERROR) {
                builder_.putToElement(elem, builder_.createName("language"), lang);
            }
        }
    }

    // collect raw content until \end{name}
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    // build the end pattern
    std::string end_pattern = "\\end{" + name + "}";
    size_t pattern_len = end_pattern.length();

    while (!at_end()) {
        // check for \end{name}
        if (remaining() >= pattern_len &&
            strncmp(pos_, end_pattern.c_str(), pattern_len) == 0) {
            pos_ += pattern_len;
            break;
        }

        stringbuf_append_char(sb, advance());
    }

    // add raw content
    if (sb->length > 0) {
        Item text = create_text(sb->str->chars, sb->length);
        if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
            list_push((List*)elem, text);
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

// =============================================================================
// Math Environment
// =============================================================================

Item LatexParser::parse_math_environment_content(const std::string& name) {
    Element* elem = builder_.element(name.c_str()).final().element;
    if (!elem) {
        return ItemError;
    }

    // for some environments like array, parse column spec
    const EnvironmentSpec* spec = find_environment(name.c_str());
    if (spec && spec->arg_spec && spec->arg_spec[0]) {
        skip_spaces();
        if (peek() == '{') {
            advance();
            Item arg = parse_balanced_content('}');
            if (arg.item != ITEM_NULL && arg.item != ITEM_ERROR) {
                list_push((List*)elem, arg);
            }
        }
    }

    // collect raw math content until \end{name}
    StringBuf* sb = ctx_.sb;
    stringbuf_reset(sb);

    std::string end_pattern = "\\end{" + name + "}";
    size_t pattern_len = end_pattern.length();

    while (!at_end()) {
        if (remaining() >= pattern_len &&
            strncmp(pos_, end_pattern.c_str(), pattern_len) == 0) {
            pos_ += pattern_len;
            break;
        }

        stringbuf_append_char(sb, advance());
    }

    // delegate to math parser
    if (sb->length > 0) {
        Input* math_input = InputManager::create_input((Url*)input_->url);
        if (math_input) {
            stringbuf_reset(ctx_.sb);  // reset before calling

            parse_math(math_input, sb->str->chars, "latex");

            stringbuf_reset(ctx_.sb);  // reset after calling

            if (math_input->root.item != ITEM_NULL) {
                list_push((List*)elem, math_input->root);
            } else {
                // fallback: store raw text
                Item text = create_text(sb->str->chars, sb->length);
                if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
                    list_push((List*)elem, text);
                }
            }

            // cleanup
            if (math_input->type_list) {
                arraylist_free(math_input->type_list);
            }
            pool_destroy(math_input->pool);
            free(math_input);
        } else {
            // fallback: store raw text
            Item text = create_text(sb->str->chars, sb->length);
            if (text.item != ITEM_NULL && text.item != ITEM_ERROR) {
                list_push((List*)elem, text);
            }
        }
    }

    ((TypeElmt*)elem->type)->content_length = ((List*)elem)->length;
    return Item{.element = elem};
}

} // namespace latex
} // namespace lambda
