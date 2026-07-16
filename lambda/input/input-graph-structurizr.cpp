#include "input-graph.h"
#include "../../lib/arraylist.h"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include <string.h>

using namespace lambda;

static const size_t STRUCTURIZR_MAX_DEPTH = 256;
static const size_t STRUCTURIZR_MAX_STATEMENTS = 100000;
static const size_t STRUCTURIZR_MAX_TOKEN = 1024 * 1024;

enum StructurizrContext {
    SZ_ROOT, SZ_WORKSPACE, SZ_MODEL, SZ_ELEMENT, SZ_DEPLOYMENT,
    SZ_ARCHETYPES, SZ_VIEWS, SZ_VIEW, SZ_STYLES, SZ_STYLE, SZ_GENERIC
};

struct StructurizrToken {
    String* value;
    const char* kind;
    SourceLocation start;
    SourceLocation end;
};

struct StructurizrParser {
    InputContext& ctx;
    SourceTracker& src;
    size_t statement_index;

    StructurizrParser(InputContext& context)
        : ctx(context), src(context.tracker), statement_index(0) {}

    void skip() { skip_wsc(src, "//", "#", true); }

    void skip_inline() {
        while (!src.atEnd() && (src.current() == ' ' || src.current() == '\t' ||
               src.current() == '\r')) src.advance();
    }

    static bool text_is(const String* value, const char* expected) {
        return value && strcmp(value->chars, expected) == 0;
    }

    static StructurizrToken* token_at(ArrayList* tokens, int index) {
        return index >= 0 && index < tokens->length
            ? (StructurizrToken*)arraylist_get(tokens, index) : nullptr;
    }

    StructurizrToken* make_token(String* value, const char* kind,
                                 SourceLocation start, SourceLocation end) {
        StructurizrToken* token = (StructurizrToken*)pool_calloc(
            ctx.input()->pool, sizeof(StructurizrToken));
        if (!token) return nullptr;
        token->value = value;
        token->kind = kind;
        token->start = start;
        token->end = end;
        return token;
    }

    size_t relationship_archetype_length() {
        if (!src.match("--") || !(str_char_is_alpha(src.peek(2)) || src.peek(2) == '_'))
            return 0;
        size_t name_length = 1;
        while (str_char_is_ident(src.peek(2 + name_length))) name_length++;
        return src.peek(2 + name_length) == '-' && src.peek(3 + name_length) == '>'
            ? name_length : 0;
    }

    StructurizrToken* token() {
        skip_inline();
        if (src.atEnd() || src.current() == '\n' || src.current() == '{' ||
            src.current() == '}' || src.current() == ';') return nullptr;
        SourceLocation start = src.location();
        if (src.current() == '"') {
            String* value = parse_shared_quoted_string(ctx);
            return value ? make_token(value, "quoted", start, src.location()) : nullptr;
        }
        if (src.match("->")) {
            src.advance(2);
            return make_token(ctx.builder.createString("->", 2), "operator", start, src.location());
        }
        size_t archetype_length = relationship_archetype_length();
        if (archetype_length > 0) {
            String* name = ctx.builder.createString(src.rest() + 2, archetype_length);
            src.advance(archetype_length + 4);
            return make_token(name, "relationship-archetype", start, src.location());
        }
        if (src.current() == '=' && src.peek(1) != '=') {
            src.advance();
            return make_token(ctx.builder.createString("=", 1), "operator", start, src.location());
        }

        const char* begin = src.rest();
        while (!src.atEnd() && src.offset() - start.offset < STRUCTURIZR_MAX_TOKEN) {
            char current = src.current();
            const char* cursor = src.rest();
            bool expression_equals = current == '=' &&
                (src.peek(1) == '=' || (cursor > begin && cursor[-1] == '='));
            if (current == ' ' || current == '\t' || current == '\r' || current == '\n' ||
                current == '{' || current == '}' || current == ';' || current == '"' ||
                src.match("->") || relationship_archetype_length() > 0 ||
                (current == '=' && !expression_equals)) break;
            src.advance();
        }
        size_t length = (size_t)(src.rest() - begin);
        if (length == STRUCTURIZR_MAX_TOKEN) {
            ctx.addErrorCode(start, "structurizr.token-limit",
                "Structurizr token exceeds the maximum length");
        }
        return length ? make_token(ctx.builder.createString(begin, length), "bare",
            start, src.location()) : nullptr;
    }

    ArrayList* line_tokens(bool* has_block) {
        ArrayList* tokens = arraylist_new(8);
        *has_block = false;
        while (!src.atEnd()) {
            skip_inline();
            if (src.match("//") || (src.current() == '#' && tokens->length == 0)) {
                skip_to_eol(src);
                break;
            }
            if (src.current() == '\n') {
                src.advance();
                break;
            }
            if (src.current() == ';') {
                src.advance();
                break;
            }
            if (src.current() == '}') break;
            if (src.current() == '{') {
                src.advance();
                *has_block = true;
                break;
            }
            StructurizrToken* next = token();
            if (!next) {
                ctx.addErrorCode(src.location(), "structurizr.syntax",
                    "Unable to read Structurizr statement token");
                skip_to_eol(src);
                break;
            }
            arraylist_append(tokens, next);
        }
        return tokens;
    }

    static bool declaration_keyword(const char* word) {
        static const char* words[] = {
            "person", "softwareSystem", "container", "component", "element",
            "group", "deploymentEnvironment", "deploymentGroup", "deploymentNode",
            "infrastructureNode", "softwareSystemInstance", "containerInstance",
            "instanceOf", "archetype", "relationship"
        };
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
            if (strcmp(word, words[i]) == 0) return true;
        return false;
    }

    static bool view_keyword(const char* word) {
        static const char* words[] = {
            "systemLandscape", "systemContext", "container", "component", "filtered",
            "dynamic", "deployment", "custom", "image"
        };
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
            if (strcmp(word, words[i]) == 0) return true;
        return false;
    }

    static const char* statement_tag(StructurizrContext parent, const char* keyword,
                                     bool relationship) {
        if (relationship) return "relationship";
        if (strcmp(keyword, "workspace") == 0) return "workspace";
        if (strcmp(keyword, "model") == 0) return "model";
        if (strcmp(keyword, "views") == 0) return "views";
        if (strcmp(keyword, "styles") == 0) return "styles";
        if (strcmp(keyword, "archetypes") == 0) return "archetypes";
        if (parent == SZ_ARCHETYPES) return "archetype";
        if (parent == SZ_VIEWS && view_keyword(keyword)) return "view";
        if (parent == SZ_STYLES &&
            (strcmp(keyword, "element") == 0 || strcmp(keyword, "relationship") == 0))
            return "style-rule";
        if (declaration_keyword(keyword)) return "declaration";
        if (strcmp(keyword, "include") == 0 || strcmp(keyword, "exclude") == 0)
            return keyword;
        if (strcmp(keyword, "autoLayout") == 0) return "auto-layout";
        if (strcmp(keyword, "properties") == 0) return "properties";
        if (strcmp(keyword, "perspectives") == 0) return "perspectives";
        if (strcmp(keyword, "animation") == 0) return "animation";
        return "statement";
    }

    static StructurizrContext child_context(const char* tag, const char* keyword) {
        if (strcmp(tag, "workspace") == 0) return SZ_WORKSPACE;
        if (strcmp(tag, "model") == 0) return SZ_MODEL;
        if (strcmp(tag, "views") == 0) return SZ_VIEWS;
        if (strcmp(tag, "view") == 0) return SZ_VIEW;
        if (strcmp(tag, "styles") == 0) return SZ_STYLES;
        if (strcmp(tag, "archetypes") == 0) return SZ_ARCHETYPES;
        if (strcmp(tag, "style-rule") == 0) return SZ_STYLE;
        if (strcmp(tag, "declaration") == 0) {
            return strstr(keyword, "deployment") || strstr(keyword, "Instance")
                ? SZ_DEPLOYMENT : SZ_ELEMENT;
        }
        return SZ_GENERIC;
    }

    static bool block_required(const char* keyword) {
        return strcmp(keyword, "workspace") == 0 || strcmp(keyword, "model") == 0 ||
            strcmp(keyword, "views") == 0 || strcmp(keyword, "styles") == 0 ||
            strcmp(keyword, "archetypes") == 0;
    }

    void add_arguments(Element* owner, ArrayList* tokens, int start) {
        int output_index = 0;
        for (int i = start; i < tokens->length; i++) {
            StructurizrToken* value = token_at(tokens, i);
            if (!value || text_is(value->value, "=")) continue;
            Element* argument = ctx.builder.element("argument")
                .attr("index", (int64_t)output_index++)
                .attr("kind", value->kind).attr("value", value->value->chars)
                .final().element;
            graph_set_source_span(ctx, argument, value->start, value->end);
            add_node_to_graph(ctx.input(), owner, argument);
        }
    }

    void parse_block(Element* owner, StructurizrContext context, size_t depth,
                     SourceLocation start) {
        if (depth >= STRUCTURIZR_MAX_DEPTH) {
            ctx.addErrorCode(start, "structurizr.depth-limit",
                "Structurizr block nesting limit exceeded");
            recover_block();
            return;
        }
        statements(owner, context, depth + 1);
        skip();
        if (src.current() == '}') src.advance();
        else ctx.addErrorCode(src.location(), "structurizr.missing-close",
            "Expected '}' to close Structurizr block");
    }

    Element* statement(StructurizrContext parent, size_t depth) {
        SourceLocation start = src.location();
        bool has_block = false;
        ArrayList* tokens = line_tokens(&has_block);
        if (!tokens || tokens->length == 0) {
            if (!tokens || !has_block) {
                if (tokens) arraylist_free(tokens);
                return nullptr;
            }
            if (statement_index >= STRUCTURIZR_MAX_STATEMENTS) {
                ctx.addErrorCode(start, "structurizr.statement-limit",
                    "Structurizr statement limit exceeded");
                arraylist_free(tokens);
                return nullptr;
            }
            // anonymous view blocks carry dynamic parallel nesting in the source DSL.
            const char* tag = parent == SZ_VIEW ? "parallel" : "block";
            Element* result = ctx.builder.element(tag)
                .attr("keyword", tag).attr("statement-index", (int64_t)statement_index++)
                .final().element;
            arraylist_free(tokens);
            parse_block(result, parent, depth, start);
            graph_set_source_span(ctx, result, start, src.location());
            return result;
        }
        if (statement_index >= STRUCTURIZR_MAX_STATEMENTS) {
            ctx.addErrorCode(start, "structurizr.statement-limit",
                "Structurizr statement limit exceeded");
            arraylist_free(tokens);
            return nullptr;
        }

        int first = 0;
        StructurizrToken* identifier = nullptr;
        if (tokens->length > 2 && text_is(token_at(tokens, 1)->value, "=")) {
            identifier = token_at(tokens, 0);
            first = 2;
        }
        int arrow = -1;
        for (int i = first; i < tokens->length; i++) {
            StructurizrToken* candidate = token_at(tokens, i);
            if (text_is(candidate->value, "->") ||
                strcmp(candidate->kind, "relationship-archetype") == 0) {
                arrow = i;
                break;
            }
        }
        StructurizrToken* keyword_token = token_at(tokens, first);
        const char* keyword = arrow >= 0 ? "relationship" : keyword_token->value->chars;
        const bool archetype_definition = parent == SZ_ARCHETYPES;
        const char* tag = archetype_definition ? "archetype"
            : statement_tag(parent, keyword, arrow >= 0);
        ElementBuilder builder = ctx.builder.element(tag);
        builder.attr("keyword", keyword).attr("statement-index", (int64_t)statement_index++);
        if (identifier) builder.attr("identifier", identifier->value->chars);
        if (strcmp(tag, "workspace") == 0)
            builder.attr("flavor", "structurizr").attr("ir-stage", "source");
        if (strcmp(tag, "view") == 0) builder.attr("kind", keyword);
        if (strcmp(tag, "style-rule") == 0) builder.attr("target-kind", keyword);
        if (archetype_definition) {
            StructurizrToken* base = token_at(tokens, arrow >= 0 ? arrow : first);
            builder.attr("target-kind", arrow >= 0 ? "relationship" : "element")
                .attr("base", text_is(base->value, "->") ? "relationship" : base->value->chars);
        }
        if (arrow >= 0 && !archetype_definition) {
            StructurizrToken* operator_token = token_at(tokens, arrow);
            if (strcmp(operator_token->kind, "relationship-archetype") == 0)
                builder.attr("archetype", operator_token->value->chars);
            // an authored dynamic order precedes the source endpoint, not the label args.
            StructurizrToken* explicit_order = arrow > first + 1
                ? token_at(tokens, first) : nullptr;
            size_t order_length = explicit_order ? explicit_order->value->len : 0;
            if (order_length > 1 && explicit_order->value->chars[order_length - 1] == ':') {
                String* order = ctx.builder.createString(
                    explicit_order->value->chars, order_length - 1);
                builder.attr("order", order->chars);
            }
            const char* from = arrow > first ? token_at(tokens, arrow - 1)->value->chars : "this";
            const char* to = arrow + 1 < tokens->length
                ? token_at(tokens, arrow + 1)->value->chars : "";
            builder.attr("from", from).attr("to", to);
        }
        Element* result = builder.final().element;
        int argument_start = arrow >= 0 ? arrow + 2 : first + 1;
        add_arguments(result, tokens, argument_start);
        arraylist_free(tokens);

        if (block_required(keyword) && !has_block) {
            ctx.addErrorCode(start, "structurizr.missing-block",
                "Structurizr '%s' statement requires a block", keyword);
        }
        if (has_block) parse_block(result, child_context(tag, keyword), depth, start);
        graph_set_source_span(ctx, result, start, src.location());
        return result;
    }

    void recover_block() {
        size_t depth = 1;
        while (!src.atEnd() && depth > 0) {
            if (src.current() == '{') depth++;
            else if (src.current() == '}') depth--;
            src.advance();
        }
    }

    void statements(Element* parent, StructurizrContext context, size_t depth) {
        while (!src.atEnd() && !ctx.shouldStopParsing()) {
            skip();
            if (src.atEnd() || src.current() == '}') return;
            size_t before = src.offset();
            Element* value = statement(context, depth);
            if (value) add_node_to_graph(ctx.input(), parent, value);
            if (src.offset() == before) {
                // malformed statements must always advance or recovery loops forever.
                ctx.addErrorCode(src.location(), "structurizr.no-progress",
                    "Unable to recover Structurizr statement");
                src.advance();
            }
        }
    }

    Element* workspace() {
        skip();
        SourceLocation start = src.location();
        Element* first = statement(SZ_ROOT, 0);
        if (first && strcmp(ElementReader(first).tagName(), "workspace") == 0) {
            skip();
            if (!src.atEnd()) {
                ctx.addErrorCode(src.location(), "structurizr.trailing-source",
                    "Unexpected source after Structurizr workspace");
            }
            graph_append_diagnostics(ctx, first, "structurizr.syntax");
            return first;
        }

        // A stable workspace root lets callers inspect diagnostics after root recovery.
        Element* root = ctx.builder.element("workspace")
            .attr("flavor", "structurizr").attr("ir-stage", "source").final().element;
        ctx.addErrorCode(start, "structurizr.workspace",
            "Structurizr source must start with a workspace block");
        if (first) add_node_to_graph(ctx.input(), root, first);
        statements(root, SZ_WORKSPACE, 0);
        graph_set_source_span(ctx, root, start, src.location());
        graph_append_diagnostics(ctx, root, "structurizr.syntax");
        return root;
    }
};

void parse_graph_structurizr(Input* input, const char* structurizr_string) {
    if (!structurizr_string || !*structurizr_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    InputContext ctx(input, structurizr_string);
    StructurizrParser parser(ctx);
    input->root = {.element = parser.workspace()};
    if (ctx.hasErrors()) ctx.logErrors();
}
