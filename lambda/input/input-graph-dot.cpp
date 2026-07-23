#include "input-graph.h"
#include "../../lib/str.h"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <string.h>

using namespace lambda;

static const size_t DOT_MAX_DEPTH = 256;
static const size_t DOT_MAX_CHAIN = 4096;
static const size_t DOT_MAX_TOKEN = 1024 * 1024;

struct DotId {
    String* value;
    const char* kind;
    SourceLocation start;
    SourceLocation end;
};

struct DotSubgraph {
    Element* element;
    String* id;
};

struct DotPort {
    String* id;
    String* compass;
};

struct DotParser {
    InputContext& ctx;
    SourceTracker& src;
    size_t statement_index;
    size_t anonymous_index;

    DotParser(InputContext& context)
        : ctx(context), src(context.tracker), statement_index(0), anonymous_index(0) {}

    void skip() { skip_wsc(src, "//", "#", true); }

    static bool id_start(char c) {
        return str_char_is_alpha(c) || c == '_' || (unsigned char)c >= 0x80;
    }

    static bool id_char(char c) {
        return id_start(c) || str_char_is_digit(c);
    }

    bool at_keyword(const char* word) {
        skip();
        size_t length = strlen(word);
        return src.remaining() >= length && str_ieq_const(src.rest(), length, word) &&
            !id_char(src.peek(length));
    }

    bool keyword(const char* word) {
        if (!at_keyword(word)) return false;
        src.advance(strlen(word));
        return true;
    }

    bool take(char c) {
        skip();
        if (src.atEnd() || src.current() != c) return false;
        src.advance();
        return true;
    }

    void error(const SourceLocation& location, const char* code, const char* message) {
        ctx.addErrorCode(location, code, "%s", message);
    }

    String* generated_id(const char* prefix, size_t value) {
        stringbuf_reset(ctx.sb);
        stringbuf_append_str(ctx.sb, prefix);
        stringbuf_append_format(ctx.sb, "%zu", value);
        return ctx.builder.createString(ctx.sb->str->chars, ctx.sb->length);
    }

    DotId id() {
        skip();
        DotId result = {nullptr, nullptr, src.location(), src.location()};
        if (src.atEnd()) return result;
        const char* start = src.rest();
        char current = src.current();

        if (current == '"') {
            stringbuf_reset(ctx.sb);
            do {
                src.advance();
                while (!src.atEnd() && src.current() != '"') {
                    if (src.current() != '\\') {
                        stringbuf_append_char(ctx.sb, src.current());
                        src.advance();
                        continue;
                    }
                    src.advance();
                    if (src.atEnd()) break;
                    if (src.current() == '\n' || src.current() == '\r') {
                        char newline = src.current();
                        src.advance();
                        if (newline == '\r' && src.current() == '\n') src.advance();
                    } else if (src.current() == '"') {
                        stringbuf_append_char(ctx.sb, '"');
                        src.advance();
                    } else {
                        // Graphviz label escapes are interpreted after parsing.
                        stringbuf_append_char(ctx.sb, '\\');
                        stringbuf_append_char(ctx.sb, src.current());
                        src.advance();
                    }
                }
                if (src.atEnd()) {
                    error(result.start, "dot.unterminated-string", "Unterminated quoted DOT ID");
                    return result;
                }
                src.advance();
                skip();
                if (src.current() != '+') break;
                src.advance();
                skip();
                if (src.current() != '"') {
                    error(src.location(), "dot.string-concat", "Expected quoted ID after '+'");
                    break;
                }
            } while (!src.atEnd());
            result.value = ctx.builder.createString(ctx.sb->str->chars, ctx.sb->length);
            result.kind = "quoted";
        } else if (current == '<') {
            size_t depth = 0;
            char quote = 0;
            do {
                current = src.current();
                if (quote) {
                    if (current == quote) quote = 0;
                } else if (current == '"' || current == '\'') {
                    quote = current;
                } else if (current == '<') {
                    depth++;
                } else if (current == '>') {
                    depth--;
                }
                src.advance();
            } while (!src.atEnd() && depth > 0 && src.offset() - result.start.offset <= DOT_MAX_TOKEN);
            if (depth > 0) {
                error(result.start, "dot.unterminated-html-id", "Unterminated HTML-like DOT ID");
                return result;
            }
            result.value = ctx.builder.createString(start, src.rest() - start);
            result.kind = "html";
        } else {
            // DOT permits -.digits; recognizing only '-' followed by a digit
            // split the remaining source into malformed statements.
            bool numeric = str_char_is_digit(current) ||
                (current == '.' && str_char_is_digit(src.peek(1))) ||
                (current == '-' && (str_char_is_digit(src.peek(1)) ||
                    (src.peek(1) == '.' && str_char_is_digit(src.peek(2)))));
            if (numeric) {
                if (current == '-') src.advance();
                while (str_char_is_digit(src.current())) src.advance();
                if (src.current() == '.') {
                    src.advance();
                    while (str_char_is_digit(src.current())) src.advance();
                }
                result.kind = "numeric";
            } else if (id_start(current)) {
                do src.advance(); while (!src.atEnd() && id_char(src.current()));
                result.kind = "bare";
            } else {
                return result;
            }
            result.value = ctx.builder.createString(start, src.rest() - start);
        }
        result.end = src.location();
        if (result.end.offset - result.start.offset > DOT_MAX_TOKEN) {
            error(result.start, "dot.token-limit", "DOT ID exceeds the parser token limit");
            result.value = nullptr;
        }
        return result;
    }

    DotPort port() {
        DotPort result = {nullptr, nullptr};
        if (!take(':')) return result;
        DotId first = id();
        if (!first.value) {
            error(src.location(), "dot.port", "Expected port ID after ':'");
            return result;
        }
        result.id = first.value;
        if (take(':')) {
            DotId compass = id();
            if (compass.value) result.compass = compass.value;
            else error(src.location(), "dot.compass", "Expected compass point after ':'");
        }
        return result;
    }

    static void add_port(ElementBuilder& endpoint, DotPort value) {
        if (value.id) endpoint.attr("port", value.id->chars);
        if (value.compass) endpoint.attr("compass", value.compass->chars);
    }

    Element* endpoint(DotId node) {
        ElementBuilder value = ctx.builder.element("dot-endpoint");
        value.attr("kind", "node").attr("id", node.value->chars)
            .attr("source-kind", node.kind);
        add_port(value, port());
        Element* result = value.final().element;
        graph_set_source_span(ctx, result, node.start, src.location());
        return result;
    }

    Element* endpoint(DotSubgraph subgraph, const SourceLocation& start) {
        ElementBuilder value = ctx.builder.element("dot-endpoint");
        value.attr("kind", "subgraph");
        if (subgraph.id) value.attr("id", subgraph.id->chars);
        Element* result = value.final().element;
        add_node_to_graph(ctx.input(), result, subgraph.element);
        graph_set_source_span(ctx, result, start, src.location());
        return result;
    }

    bool edge_op(String** spelling = nullptr) {
        skip();
        if (!(src.match("->") || src.match("--"))) return false;
        if (spelling) *spelling = ctx.builder.createString(src.rest(), 2);
        src.advance(2);
        return true;
    }

    void attr_lists(Element* owner) {
        size_t list_index = 0;
        while (true) {
            skip();
            if (src.current() != '[') return;
            SourceLocation list_start = src.location();
            src.advance();
            Element* properties = ctx.builder.element("properties")
                .attr("namespace", "graphviz")
                .attr("source-list-index", (int64_t)list_index++)
                .final().element;
            while (!src.atEnd()) {
                skip();
                if (take(']')) break;
                SourceLocation property_start = src.location();
                DotId name = id();
                if (!name.value) {
                    error(property_start, "dot.attribute-name", "Expected DOT attribute name");
                    recover(']');
                    take(']');
                    break;
                }
                if (!take('=')) {
                    error(src.location(), "dot.attribute-equals", "Expected '=' after DOT attribute name");
                    recover(']');
                    take(']');
                    break;
                }
                DotId value = id();
                if (!value.value) {
                    error(src.location(), "dot.attribute-value", "Expected DOT attribute value");
                    recover(']');
                    take(']');
                    break;
                }
                Element* property = ctx.builder.element("property")
                    .attr("name", name.value->chars).attr("value", value.value->chars)
                    .attr("name-source-kind", name.kind).attr("value-source-kind", value.kind)
                    .final().element;
                graph_set_source_span(ctx, property, property_start, value.end);
                add_node_to_graph(ctx.input(), properties, property);
                skip();
                if (src.current() == ',' || src.current() == ';') src.advance();
            }
            graph_set_source_span(ctx, properties, list_start, src.location());
            add_node_to_graph(ctx.input(), owner, properties);
        }
    }

    void recover(char close = '}') {
        while (!src.atEnd() && src.current() != ';' && src.current() != close) src.advance();
    }

    DotSubgraph subgraph(size_t depth) {
        SourceLocation start = src.location();
        bool named_form = keyword("subgraph");
        DotId name = {nullptr, nullptr, src.location(), src.location()};
        skip();
        if (named_form && src.current() != '{') name = id();
        if (!take('{')) {
            error(src.location(), "dot.subgraph-open", "Expected '{' to start DOT subgraph");
            return {nullptr, nullptr};
        }
        if (depth >= DOT_MAX_DEPTH) {
            error(start, "dot.depth-limit", "DOT subgraph nesting limit exceeded");
            recover('}');
        }
        String* stable_id = name.value ? name.value : generated_id("dot-anonymous-", anonymous_index++);
        ElementBuilder builder = ctx.builder.element("subgraph");
        builder.attr("id", stable_id->chars).attr("role", "scope")
            .attr("source-kind", name.value ? name.kind : "anonymous");
        Element* result = builder.final().element;
        if (depth < DOT_MAX_DEPTH) statements(result, depth + 1);
        if (!take('}')) error(src.location(), "dot.subgraph-close", "Expected '}' to close DOT subgraph");
        graph_set_source_span(ctx, result, start, src.location());
        return {result, stable_id};
    }

    Element* next_endpoint(size_t depth) {
        skip();
        SourceLocation start = src.location();
        if (at_keyword("subgraph") || src.current() == '{') {
            DotSubgraph group = subgraph(depth);
            return group.element ? endpoint(group, start) : nullptr;
        }
        DotId node = id();
        if (!node.value) {
            error(start, "dot.endpoint", "Expected node or subgraph edge endpoint");
            return nullptr;
        }
        return endpoint(node);
    }

    void edge(Element* parent, Element* first, const SourceLocation& start, size_t depth) {
        Element* statement = ctx.builder.element("dot-edge-statement")
            .attr("id", generated_id("dot-stmt-", statement_index++)->chars)
            .final().element;
        add_node_to_graph(ctx.input(), statement, first);
        size_t count = 1;
        String* op = nullptr;
        while (edge_op(&op)) {
            Element* next = next_endpoint(depth);
            if (!next) break;
            add_graph_attribute(ctx.input(), next, "operator", op->chars);
            add_node_to_graph(ctx.input(), statement, next);
            if (++count > DOT_MAX_CHAIN) {
                error(start, "dot.chain-limit", "DOT edge chain limit exceeded");
                break;
            }
        }
        attr_lists(statement);
        graph_set_source_span(ctx, statement, start, src.location());
        add_node_to_graph(ctx.input(), parent, statement);
    }

    void statement(Element* parent, size_t depth) {
        skip();
        SourceLocation start = src.location();
        if (at_keyword("subgraph") || src.current() == '{') {
            DotSubgraph group = subgraph(depth);
            if (!group.element) return;
            skip();
            if (src.match("->") || src.match("--")) edge(parent, endpoint(group, start), start, depth);
            else add_node_to_graph(ctx.input(), parent, group.element);
            return;
        }

        DotId first = id();
        if (!first.value) {
            error(start, "dot.statement", "Expected DOT statement");
            recover();
            return;
        }
        skip();
        bool scope = first.kind && strcmp(first.kind, "bare") == 0 &&
            (str_ieq_const(first.value->chars, first.value->len, "graph") ||
             str_ieq_const(first.value->chars, first.value->len, "node") ||
             str_ieq_const(first.value->chars, first.value->len, "edge"));
        if (scope && src.current() == '[') {
            const char* target = str_ieq_const(first.value->chars, first.value->len, "graph")
                ? "graph" : (str_ieq_const(first.value->chars, first.value->len, "node")
                    ? "node" : "edge");
            Element* attrs = ctx.builder.element("dot-attr-statement")
                .attr("target-kind", target).final().element;
            attr_lists(attrs);
            graph_set_source_span(ctx, attrs, start, src.location());
            add_node_to_graph(ctx.input(), parent, attrs);
            return;
        }
        if (take('=')) {
            DotId value = id();
            if (!value.value) error(src.location(), "dot.assignment-value", "Expected DOT assignment value");
            ElementBuilder assignment = ctx.builder.element("dot-assignment");
            assignment.attr("name", first.value->chars).attr("name-source-kind", first.kind);
            if (value.value) assignment.attr("value", value.value->chars)
                .attr("value-source-kind", value.kind);
            Element* result = assignment.final().element;
            graph_set_source_span(ctx, result, start, src.location());
            add_node_to_graph(ctx.input(), parent, result);
            return;
        }
        ElementBuilder endpoint_builder = ctx.builder.element("dot-endpoint");
        endpoint_builder.attr("kind", "node").attr("id", first.value->chars)
            .attr("source-kind", first.kind);
        DotPort node_port = port();
        add_port(endpoint_builder, node_port);
        Element* first_endpoint = endpoint_builder.final().element;
        graph_set_source_span(ctx, first_endpoint, first.start, src.location());
        skip();
        if (src.match("->") || src.match("--")) {
            edge(parent, first_endpoint, start, depth);
            return;
        }
        ElementBuilder node_builder = ctx.builder.element("node");
        node_builder.attr("id", first.value->chars).attr("source-kind", first.kind);
        add_port(node_builder, node_port);
        Element* node = node_builder.final().element;
        attr_lists(node);
        graph_set_source_span(ctx, node, start, src.location());
        add_node_to_graph(ctx.input(), parent, node);
    }

    void statements(Element* parent, size_t depth) {
        while (!src.atEnd()) {
            skip();
            while (src.current() == ';') { src.advance(); skip(); }
            if (src.atEnd() || src.current() == '}') return;
            size_t before = src.offset();
            statement(parent, depth);
            skip();
            if (src.current() == ';') src.advance();
            if (src.offset() == before) {
                // Recovery must always consume input or malformed statements loop forever.
                error(src.location(), "dot.no-progress", "Could not advance past DOT statement");
                src.advance();
            }
            if (ctx.shouldStopParsing()) return;
        }
    }

    Element* graph() {
        skip();
        SourceLocation start = src.location();
        bool strict = keyword("strict");
        bool directed;
        if (keyword("digraph")) directed = true;
        else if (keyword("graph")) directed = false;
        else {
            error(src.location(), "dot.graph-kind", "Expected 'graph' or 'digraph'");
            directed = true;
        }
        skip();
        DotId name = {nullptr, nullptr, src.location(), src.location()};
        if (src.current() != '{') name = id();
        ElementBuilder builder = ctx.builder.element("graph");
        builder.attr("type", directed ? "directed" : "undirected")
            .attr("layout", "dot").attr("flavor", "dot").attr("version", "1")
            .attr("ir-stage", "source").attr("directed", directed).attr("strict", strict);
        if (name.value) builder.attr("id", name.value->chars).attr("source-kind", name.kind);
        Element* result = builder.final().element;
        if (!take('{')) error(src.location(), "dot.graph-open", "Expected '{' to start DOT graph");
        else statements(result, 0);
        if (!take('}')) error(src.location(), "dot.graph-close", "Expected '}' to close DOT graph");
        graph_set_source_span(ctx, result, start, src.location());
        graph_append_diagnostics(ctx, result, "dot.syntax");
        return result;
    }
};

void parse_graph_dot(Input* input, const char* dot_string) {
    if (!dot_string || !*dot_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    InputContext ctx(input, dot_string);
    DotParser parser(ctx);
    input->root = {.element = parser.graph()};
    if (ctx.hasErrors()) ctx.logErrors();
}
