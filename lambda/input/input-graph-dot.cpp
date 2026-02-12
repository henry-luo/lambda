#include "input-graph.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <ctype.h>
#include <string.h>

using namespace lambda;

// Helper: skip to end of line
static void skip_to_eol(SourceTracker& tracker) {
    while (!tracker.atEnd() && tracker.current() != '\n') {
        tracker.advance();
    }
}

// Forward declarations for internal functions
static void skip_whitespace_and_comments(SourceTracker& tracker);
static String* parse_identifier(InputContext& ctx);
static String* parse_quoted_string(InputContext& ctx);
static String* parse_attribute_value(InputContext& ctx);
static void parse_attribute_list(InputContext& ctx, Element* element);
static const int DOT_MAX_DEPTH = 256;

static Element* parse_node_statement(InputContext& ctx);
static Element* parse_edge_statement(InputContext& ctx);
static void parse_subgraph(InputContext& ctx, Element* graph, int depth = 0);

// Skip whitespace and comments
static void skip_whitespace_and_comments(SourceTracker& tracker) {
    while (!tracker.atEnd()) {
        char c = tracker.current();

        // skip whitespace
        if (isspace(c)) {
            tracker.advance();
            continue;
        }

        // skip // comments
        if (c == '/' && tracker.peek(1) == '/') {
            skip_to_eol(tracker);
            continue;
        }

        // skip /* */ comments
        if (c == '/' && tracker.peek(1) == '*') {
            tracker.advance(); // skip /
            tracker.advance(); // skip *
            while (!tracker.atEnd() && !(tracker.current() == '*' && tracker.peek(1) == '/')) {
                tracker.advance();
            }
            if (!tracker.atEnd()) {
                tracker.advance(); // skip *
                tracker.advance(); // skip /
            }
            continue;
        }

        // skip # comments (also valid in DOT)
        if (c == '#') {
            skip_to_eol(tracker);
            continue;
        }

        break;
    }
}

// Parse identifier
static String* parse_identifier(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    if (tracker.atEnd()) return nullptr;

    char c = tracker.current();
    if (!isalpha(c) && c != '_') {
        return nullptr;
    }

    const char* start = tracker.rest();
    size_t len = 0;

    while (!tracker.atEnd()) {
        c = tracker.current();
        if (isalnum(c) || c == '_') {
            tracker.advance();
            len++;
        } else {
            break;
        }
    }

    return ctx.builder.createString(start, len);
}

// Parse quoted string
static String* parse_quoted_string(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments(tracker);

    if (tracker.atEnd() || tracker.current() != '"') {
        return nullptr;
    }

    SourceLocation start_loc = tracker.location();
    tracker.advance(); // skip opening quote

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (!tracker.atEnd() && tracker.current() != '"') {
        char c = tracker.current();

        if (c == '\\') {
            tracker.advance();
            if (tracker.atEnd()) {
                ctx.addError(tracker.location(), "Unterminated string escape");
                return nullptr;
            }

            c = tracker.current();
            switch (c) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                default:
                    stringbuf_append_char(sb, '\\');
                    stringbuf_append_char(sb, c);
                    break;
            }
            tracker.advance();
        } else {
            stringbuf_append_char(sb, c);
            tracker.advance();
        }
    }

    if (tracker.atEnd()) {
        ctx.addError(start_loc, "Unterminated quoted string");
        return nullptr;
    }

    tracker.advance(); // skip closing quote

    return ctx.builder.createString(sb->str->chars, sb->length);
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

        // try to parse node or edge statement
        SourceLocation checkpoint = tracker.location();

        // look ahead to determine if this is an edge statement
        String* first_id = parse_identifier(ctx);
        if (!first_id) {
            first_id = parse_quoted_string(ctx);
        }

        if (first_id) {
            skip_whitespace_and_comments(tracker);

            // check for edge operator
            bool is_edge = !tracker.atEnd() && tracker.current() == '-' &&
                          (tracker.peek(1) == '>' || tracker.peek(1) == '-');

            // restore position
            // tracker.reset(checkpoint);

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

        // Simple lookahead: scan forward to check for edge operator without parsing
        const char* lookahead_pos = tracker.rest();
        bool is_edge = false;

        // Skip identifier/quoted string
        while (*lookahead_pos && (isalnum(*lookahead_pos) || *lookahead_pos == '_')) {
            lookahead_pos++;
        }
        if (*lookahead_pos == '"') {
            lookahead_pos++;
            while (*lookahead_pos && *lookahead_pos != '"') {
                if (*lookahead_pos == '\\') lookahead_pos++;  // skip escaped char
                lookahead_pos++;
            }
            if (*lookahead_pos == '"') lookahead_pos++;
        }

        // Skip whitespace
        while (*lookahead_pos && (*lookahead_pos == ' ' || *lookahead_pos == '\t' ||
                                   *lookahead_pos == '\n' || *lookahead_pos == '\r')) {
            lookahead_pos++;
        }

        // Check for edge operator (-> or --)
        if (*lookahead_pos == '-' && (lookahead_pos[1] == '>' || lookahead_pos[1] == '-')) {
            is_edge = true;
        }

        if (is_edge) {
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
