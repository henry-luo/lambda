#include "input-graph.h"
#include "../../lib/str.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include <ctype.h>
#include <string.h>

using namespace lambda;

// Forward declarations for internal functions
static void skip_whitespace_and_comments(SourceTracker& tracker);
static String* parse_identifier(InputContext& ctx);
static String* parse_quoted_string(InputContext& ctx);
static String* parse_attribute_value(InputContext& ctx);
static void parse_attribute_list(InputContext& ctx, Element* element);
static bool dot_statement_is_edge(SourceTracker& tracker);
static bool dot_statement_is_assignment(SourceTracker& tracker);
static bool dot_statement_is_default_attributes(SourceTracker& tracker);
static bool parse_dot_attribute_assignment(InputContext& ctx, Element* element);
static bool parse_dot_default_attributes(InputContext& ctx, Element* graph);
static const int DOT_MAX_DEPTH = 256;

static Element* parse_node_statement(InputContext& ctx);
static Element* parse_edge_statement(InputContext& ctx);
static void parse_subgraph(InputContext& ctx, Element* graph, int depth = 0);

// skip whitespace, // line comments, /* */ block comments, and # line comments
static void skip_whitespace_and_comments(SourceTracker& tracker) {
    skip_wsc(tracker, "//", "#", true);
}

// read DOT identifier: [A-Za-z_][A-Za-z0-9_]*  (whitespace skipped first)
static String* parse_identifier(InputContext& ctx) {
    skip_whitespace_and_comments(ctx.tracker);
    return read_graph_identifier(ctx, nullptr, true);
}

// parse a double-quoted string, using the shared escape handler
static String* parse_quoted_string(InputContext& ctx) {
    skip_whitespace_and_comments(ctx.tracker);
    return parse_shared_quoted_string(ctx);
}

// Parse attribute value (identifier or quoted string)
static String* parse_attribute_value(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    if (!tracker.atEnd() && tracker.current() == '"') {
        return parse_quoted_string(ctx);
    } else {
        return parse_identifier(ctx);
    }
}

static const char* dot_skip_ascii_space(const char* pos) {
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) {
        pos++;
    }
    return pos;
}

static const char* dot_skip_statement_id(const char* pos) {
    if (*pos == '"') {
        pos++;
        while (*pos && *pos != '"') {
            if (*pos == '\\' && pos[1]) pos++;
            pos++;
        }
        if (*pos == '"') pos++;
        return pos;
    }
    if (!(str_char_is_alpha(*pos) || *pos == '_')) {
        return pos;
    }
    while (*pos && (str_char_is_alnum(*pos) || *pos == '_')) {
        pos++;
    }
    return pos;
}

static bool dot_statement_is_edge(SourceTracker& tracker) {
    const char* lookahead_pos = dot_skip_statement_id(tracker.rest());
    lookahead_pos = dot_skip_ascii_space(lookahead_pos);
    return *lookahead_pos == '-' && (lookahead_pos[1] == '>' || lookahead_pos[1] == '-');
}

static bool dot_statement_is_assignment(SourceTracker& tracker) {
    const char* lookahead_pos = dot_skip_statement_id(tracker.rest());
    lookahead_pos = dot_skip_ascii_space(lookahead_pos);
    return *lookahead_pos == '=';
}

static bool dot_statement_is_default_attributes(SourceTracker& tracker) {
    const char* start = tracker.rest();
    const char* after_id = dot_skip_statement_id(start);
    size_t id_len = (size_t)(after_id - start);
    if (id_len != 4 && id_len != 5) return false;
    bool is_default_scope =
        (id_len == 4 && (strncmp(start, "node", 4) == 0 || strncmp(start, "edge", 4) == 0)) ||
        (id_len == 5 && strncmp(start, "graph", 5) == 0);
    if (!is_default_scope) return false;
    after_id = dot_skip_ascii_space(after_id);
    return *after_id == '[';
}

static bool parse_dot_attribute_assignment(InputContext& ctx, Element* element) {
    String* attr_name = parse_identifier(ctx);
    if (!attr_name) {
        attr_name = parse_quoted_string(ctx);
    }
    if (!attr_name) {
        ctx.addError(ctx.tracker.location(), "Expected graph attribute name");
        return false;
    }

    skip_whitespace_and_comments(ctx.tracker);
    if (ctx.tracker.atEnd() || ctx.tracker.current() != '=') {
        ctx.addError(ctx.tracker.location(), "Expected '=' after graph attribute name");
        return false;
    }
    ctx.tracker.advance();

    String* attr_value = parse_attribute_value(ctx);
    if (!attr_value) {
        ctx.addError(ctx.tracker.location(), "Expected graph attribute value");
        return false;
    }
    add_graph_attribute(ctx.input(), element, attr_name->chars, attr_value->chars);
    return true;
}

static bool parse_dot_default_attributes(InputContext& ctx, Element* graph) {
    String* scope = parse_identifier(ctx);
    if (!scope) {
        ctx.addError(ctx.tracker.location(), "Expected DOT default attribute scope");
        return false;
    }

    MarkBuilder builder(ctx.input());
    ElementBuilder defaults = builder.element("defaults");
    defaults.attr("scope", scope->chars);
    Element* defaults_el = defaults.final().element;

    // DOT default-attribute statements are not nodes; representing them
    // separately avoids corrupting the pooled node element shape.
    parse_attribute_list(ctx, defaults_el);
    add_node_to_graph(ctx.input(), graph, defaults_el);
    return true;
}

// Parse attribute list [attr1=value1, attr2=value2, ...]
static void parse_attribute_list(InputContext& ctx, Element* element) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    if (tracker.atEnd() || tracker.current() != '[') {
        return;
    }

    tracker.advance(); // skip [

    while (!tracker.atEnd() && tracker.current() != ']') {
        skip_whitespace_and_comments(tracker);

        if (tracker.current() == ']') break;

        // parse attribute name
        String* attr_name = parse_identifier(ctx);
        if (!attr_name) {
            ctx.addError(tracker.location(), "Expected attribute name");
            break;
        }

        skip_whitespace_and_comments(tracker);

        // expect =
        if (tracker.atEnd() || tracker.current() != '=') {
            ctx.addError(tracker.location(), "Expected '=' after attribute name");
            break;
        }
        tracker.advance();

        skip_whitespace_and_comments(tracker);

        // parse attribute value
        String* attr_value = parse_attribute_value(ctx);
        if (!attr_value) {
            ctx.addError(tracker.location(), "Expected attribute value");
            break;
        }

        // add attribute to element
        add_graph_attribute(ctx.input(), element, attr_name->chars, attr_value->chars);

        skip_whitespace_and_comments(tracker);

        // skip comma if present
        if (!tracker.atEnd() && tracker.current() == ',') {
            tracker.advance();
        }
    }

    if (!tracker.atEnd() && tracker.current() == ']') {
        tracker.advance(); // skip ]
    } else {
        ctx.addError(tracker.location(), "Expected ']' to close attribute list");
    }
}

// Parse node statement: node_id [attributes]
static Element* parse_node_statement(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    // parse node ID
    String* node_id = parse_identifier(ctx);
    if (!node_id) {
        node_id = parse_quoted_string(ctx);
    }
    if (!node_id) {
        ctx.addError(tracker.location(), "Expected node identifier");
        return nullptr;
    }

    // create node element
    Element* node = create_node_element(ctx.input(), node_id->chars, node_id->chars);

    // parse optional attributes
    parse_attribute_list(ctx, node);

    return node;
}

// Parse edge statement: node1 -> node2 [attributes] or node1 -- node2 [attributes]
static Element* parse_edge_statement(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    // parse from node
    String* from_id = parse_identifier(ctx);
    if (!from_id) {
        from_id = parse_quoted_string(ctx);
    }
    if (!from_id) {
        ctx.addError(tracker.location(), "Expected source node identifier for edge");
        return nullptr;
    }

    skip_whitespace_and_comments(tracker);

    // parse edge operator (-> or --)
    bool is_directed = false;
    if (tracker.atEnd() || tracker.current() != '-') {
        ctx.addError(tracker.location(), "Expected edge operator (-> or --)");
        return nullptr;
    }

    tracker.advance(); // skip first -

    if (tracker.atEnd()) {
        ctx.addError(tracker.location(), "Incomplete edge operator");
        return nullptr;
    }

    char next = tracker.current();
    if (next == '>') {
        tracker.advance();
        is_directed = true;
    } else if (next == '-') {
        tracker.advance();
        is_directed = false;
    } else {
        ctx.addError(tracker.location(), "Invalid edge operator, expected -> or --");
        return nullptr;
    }

    skip_whitespace_and_comments(tracker);

    // parse to node
    String* to_id = parse_identifier(ctx);
    if (!to_id) {
        to_id = parse_quoted_string(ctx);
    }
    if (!to_id) {
        ctx.addError(tracker.location(), "Expected target node identifier for edge");
        return nullptr;
    }

    // create edge element
    Element* edge = create_edge_element(ctx.input(), from_id->chars, to_id->chars, nullptr);

    // add direction attribute with CSS-aligned naming
    add_graph_attribute(ctx.input(), edge, "direction", is_directed ? "forward" : "none");

    // parse optional attributes
    parse_attribute_list(ctx, edge);

    return edge;
}

// Parse subgraph or cluster
static void parse_subgraph(InputContext& ctx, Element* graph, int depth) {
    SourceTracker& tracker = ctx.tracker;

    if (depth >= DOT_MAX_DEPTH) {
        ctx.addError(tracker.location(), "Maximum DOT subgraph nesting depth (%d) exceeded", DOT_MAX_DEPTH);
        return;
    }

    skip_whitespace_and_comments(tracker);

    // look for "subgraph" or "cluster"
    if (tracker.match("subgraph")) {
        tracker.advance(8);
    } else if (tracker.match("cluster")) {
        tracker.advance(7);
    } else {
        return;
    }

    skip_whitespace_and_comments(tracker);

    // parse optional subgraph name
    String* subgraph_id = parse_identifier(ctx);
    if (!subgraph_id) {
        subgraph_id = parse_quoted_string(ctx);
    }

    // default ID if none provided
    if (!subgraph_id) {
        subgraph_id = ctx.builder.createString("subgraph");
    }

    skip_whitespace_and_comments(tracker);

    // expect {
    if (tracker.atEnd() || tracker.current() != '{') {
        ctx.addError(tracker.location(), "Expected '{' to start subgraph body");
        return;
    }
    tracker.advance();

    // create cluster element
    Element* cluster = create_cluster_element(ctx.input(), subgraph_id->chars, subgraph_id->chars);

    // parse statements within subgraph
    while (!tracker.atEnd() && tracker.current() != '}') {
        skip_whitespace_and_comments(tracker);

        if (tracker.current() == '}') {
            break;
        }

        // DOT lookahead must not consume parser state; consuming the first id
        // here made subgraph nodes restart at the following ';' or '=' token.
        SourceLocation checkpoint = tracker.location();

        if (dot_statement_is_default_attributes(tracker)) {
            parse_dot_default_attributes(ctx, cluster);
        } else if (dot_statement_is_assignment(tracker)) {
            parse_dot_attribute_assignment(ctx, cluster);
        } else {
            bool is_edge = dot_statement_is_edge(tracker);
            if (is_edge) {
                // this is an edge statement
                Element* edge = parse_edge_statement(ctx);
                if (edge) {
                    add_edge_to_graph(ctx.input(), cluster, edge);
                }
            } else {
                // this is a node statement
                Element* node = parse_node_statement(ctx);
                if (node) {
                    add_node_to_graph(ctx.input(), cluster, node);
                }
            }
        }

        skip_whitespace_and_comments(tracker);

        // skip optional semicolon
        if (!tracker.atEnd() && tracker.current() == ';') {
            tracker.advance();
        }

        // prevent infinite loop
        if (tracker.location().offset == checkpoint.offset) {
            tracker.advance();
            if (ctx.shouldStopParsing()) break;
        }
    }

    if (!tracker.atEnd() && tracker.current() == '}') {
        tracker.advance(); // skip }
    } else {
        ctx.addError(tracker.location(), "Expected '}' to close subgraph");
    }

    // add cluster to graph
    add_cluster_to_graph(ctx.input(), graph, cluster);
}

// Main DOT parser function
void parse_graph_dot(Input* input, const char* dot_string) {
    if (!dot_string || !*dot_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    InputContext ctx(input, dot_string);
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    // parse graph type (strict? (di)graph name? { ... })
    bool is_strict = false;
    bool is_directed = false;
    String* graph_name = nullptr;

    // check for "strict"
    if (tracker.match("strict") && !tracker.atEnd() &&
        (tracker.peek(6) == ' ' || tracker.peek(6) == '\t' || tracker.peek(6) == '\n')) {
        is_strict = true;
        tracker.advance(6);
        skip_whitespace_and_comments(tracker);
    }

    // check for "digraph" or "graph"
    if (tracker.match("digraph") && !tracker.atEnd() &&
        (tracker.peek(7) == ' ' || tracker.peek(7) == '\t' || tracker.peek(7) == '\n' ||
         tracker.peek(7) == '{')) {
        is_directed = true;
        tracker.advance(7);
    } else if (tracker.match("graph") && !tracker.atEnd() &&
               (tracker.peek(5) == ' ' || tracker.peek(5) == '\t' || tracker.peek(5) == '\n' ||
                tracker.peek(5) == '{')) {
        is_directed = false;
        tracker.advance(5);
    } else {
        ctx.addError(tracker.location(), "Expected 'graph' or 'digraph' keyword");

        return;
    }

    skip_whitespace_and_comments(tracker);

    // parse optional graph name
    graph_name = parse_identifier(ctx);
    if (!graph_name) {
        graph_name = parse_quoted_string(ctx);
    }

    skip_whitespace_and_comments(tracker);

    // expect {
    if (tracker.atEnd() || tracker.current() != '{') {
        ctx.addError(tracker.location(), "Expected '{' to start graph body");

        return;
    }
    tracker.advance();

    // create main graph element
    Element* graph = create_graph_element(input,
        is_directed ? "directed" : "undirected",
        "dot",
        "dot");

    // add graph attributes with CSS-aligned naming
    if (graph_name) {
        add_graph_attribute(input, graph, "name", graph_name->chars);
    }
    if (is_strict) {
        add_graph_attribute(input, graph, "strict", "true");
    }
    // add the directed attribute as a boolean
    add_graph_attribute(input, graph, "directed", is_directed ? "true" : "false");

    // parse graph statements
    while (!tracker.atEnd() && tracker.current() != '}') {
        skip_whitespace_and_comments(tracker);

        if (tracker.current() == '}') {
            break;
        }

        // check for subgraph
        if (tracker.match("subgraph") || tracker.match("cluster")) {
            parse_subgraph(ctx, graph);
            continue;
        }

        // try to parse node or edge statement
        SourceLocation checkpoint = tracker.location();

        if (dot_statement_is_default_attributes(tracker)) {
            parse_dot_default_attributes(ctx, graph);
        } else if (dot_statement_is_assignment(tracker)) {
            parse_dot_attribute_assignment(ctx, graph);
        } else if (dot_statement_is_edge(tracker)) {
            // this is an edge statement
            Element* edge = parse_edge_statement(ctx);
            if (edge) {
                add_edge_to_graph(input, graph, edge);
            }
        } else {
            // this is a node statement
            Element* node = parse_node_statement(ctx);
            if (node) {
                add_node_to_graph(input, graph, node);
            }
        }

        skip_whitespace_and_comments(tracker);


        // skip optional semicolon
        if (!tracker.atEnd() && tracker.current() == ';') {
            tracker.advance();
        }

        // prevent infinite loop
        if (tracker.location().offset == checkpoint.offset) {
            tracker.advance();
            if (ctx.shouldStopParsing()) break;
        }
    }

    if (!tracker.atEnd() && tracker.current() == '}') {
        tracker.advance(); // skip }
    } else {
        ctx.addError(tracker.location(), "Expected '}' to close graph");
    }

    // set result
    input->root = {.element = graph};

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
