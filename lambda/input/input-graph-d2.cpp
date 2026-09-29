#include "input-graph.h"
#include "../../lib/str.h"
#include "../io/mark_builder.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"

using namespace lambda;

// D2 parser implementation
// D2 (https://d2lang.com/) is a modern diagramming language with clean syntax
// Examples:
//   x -> y
//   x -> y: Label
//   x.shape: circle
//   x.style: {fill: red; stroke: blue}

// Forward declarations for D2 parsing
static void skip_whitespace_and_comments_d2(SourceTracker& tracker);
static String* parse_d2_identifier(InputContext& ctx);
static String* parse_d2_quoted_string(InputContext& ctx);
static String* parse_d2_label(InputContext& ctx);
static void parse_d2_style_block(InputContext& ctx, Element* element);
static bool parse_d2_property_assignment(InputContext& ctx, Element* graph,
                                         const char* first_id, const SourceLocation& source_start);
static bool parse_d2_edge(InputContext& ctx, Element* graph,
                          const char* first_id, const SourceLocation& first_start);
static bool parse_d2_node_with_block(InputContext& ctx, Element* graph,
                                     const char* first_id, const SourceLocation& source_start);

// skip whitespace and # line comments
static void skip_whitespace_and_comments_d2(SourceTracker& tracker) {
    skip_wsc(tracker, "#", nullptr, false);
}

// read a D2 identifier, including dotted paths (no alpha-start requirement)
static String* parse_d2_identifier(InputContext& ctx) {
    skip_whitespace_and_comments_d2(ctx.tracker);
    return read_graph_identifier(ctx, "-.", false);
}

// A dotted node path remains an ID unless its suffix names a supported property.
static const char* d2_property_separator(const char* id) {
    const char* styled = strstr(id, ".style.");
    if (styled && styled[7] != '\0') return styled;
    const char* last = strrchr(id, '.');
    if (!last) return nullptr;
    const char* property = last + 1;
    if (strcmp(property, "shape") == 0 || strcmp(property, "label") == 0 ||
        strcmp(property, "style") == 0) return last;
    return nullptr;
}

// parse a double-quoted string, using the shared escape handler
static String* parse_d2_quoted_string(InputContext& ctx) {
    return parse_shared_quoted_string(ctx);
}

// Parse D2 label (quoted or unquoted)
static String* parse_d2_label(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments_d2(tracker);

    if (tracker.atEnd()) return nullptr;

    if (tracker.current() == '"') {
        return parse_d2_quoted_string(ctx);
    }

    // parse unquoted label until end of line or special character
    const char* start = tracker.rest();
    size_t len = 0;

    while (!tracker.atEnd()) {
        char c = tracker.current();
        if (c == '\n' || c == '\r' || c == '{' || c == '}' || c == '#' || c == ';') {
            break;
        }
        tracker.advance();
        len++;
    }

    const char* trimmed = start;
    size_t trimmed_len = len;
    str_rtrim(&trimmed, &trimmed_len);
    len = trimmed_len;

    if (len == 0) return nullptr;

    return ctx.builder.createString(start, len);
}

// Parse D2 style block { property: value; ... }
static void parse_d2_style_block(InputContext& ctx, Element* element) {
    SourceTracker& tracker = ctx.tracker;

    if (tracker.atEnd() || tracker.current() != '{') {
        return;
    }

    tracker.advance(); // skip opening brace

    while (!tracker.atEnd() && tracker.current() != '}') {
        skip_whitespace_and_comments_d2(tracker);

        if (tracker.atEnd() || tracker.current() == '}') {
            break;
        }

        // parse property: value
        String* property = parse_d2_identifier(ctx);
        if (!property) {
            ctx.addError(tracker.location(), "Expected property name in style block");
            break;
        }

        skip_whitespace_and_comments_d2(tracker);

        if (tracker.atEnd() || tracker.current() != ':') {
            ctx.addError(tracker.location(), "Expected ':' after property name");
            break;
        }

        tracker.advance(); // skip colon

        skip_whitespace_and_comments_d2(tracker);

        if (!tracker.atEnd() && tracker.current() == '{') {
            // D2's nested style block contributes attributes to the same node.
            parse_d2_style_block(ctx, element);
        } else {
            String* value = parse_d2_label(ctx);
            if (value) {
                const char* name = strcmp(property->chars, "stroke-dash") == 0
                    ? "stroke-dasharray" : property->chars;
                add_graph_attribute(ctx.input(), element, name, value->chars);
            }
        }

        skip_whitespace_and_comments_d2(tracker);

        // skip optional semicolon
        if (!tracker.atEnd() && tracker.current() == ';') {
            tracker.advance();
        }
    }

    if (!tracker.atEnd() && tracker.current() == '}') {
        tracker.advance(); // skip closing brace
    } else {
        ctx.addError(tracker.location(), "Expected '}' to close style block");
    }
}

// Parse node property assignment: node.property: value
static bool parse_d2_property_assignment(InputContext& ctx,
                                         Element* graph, const char* first_id,
                                         const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    const char* separator = d2_property_separator(first_id);
    if (!separator || tracker.atEnd() || tracker.current() != ':') return false;
    String* node_id = ctx.builder.createString(first_id, separator - first_id);
    const char* property = separator + (strncmp(separator, ".style.", 7) == 0 ? 7 : 1);

    tracker.advance(); // skip colon

    skip_whitespace_and_comments_d2(tracker);

    // find or create the node
    Element* node = node_id ? create_node_element(ctx.input(), node_id->chars, nullptr) : nullptr;
    if (node) {
        graph_set_source_span(ctx, node, source_start, tracker.location());
        add_node_to_graph(ctx.input(), graph, node);

        if (!tracker.atEnd() && tracker.current() == '{') {
            // style block
            parse_d2_style_block(ctx, node);
        } else {
            // single property value
            String* value = parse_d2_label(ctx);
            if (value) {
                const char* name = strcmp(property, "stroke-dash") == 0
                    ? "stroke-dasharray" : property;
                add_graph_attribute(ctx.input(), node, name, value->chars);
            }
        }
    }

    return true;
}

// Parse edge: node1 -> node2 [: label]
static bool parse_d2_edge(InputContext& ctx,
                          Element* graph, const char* first_id,
                          const SourceLocation& first_start) {
    SourceTracker& tracker = ctx.tracker;

    if (tracker.remaining() < 2 || tracker.current() != '-' || tracker.peek(1) != '>') {
        return false;
    }

    tracker.advance(); // skip -
    tracker.advance(); // skip >

    skip_whitespace_and_comments_d2(tracker);

    SourceLocation second_start = tracker.location();
    String* second_id = parse_d2_identifier(ctx);
    if (!second_id) {
        ctx.addError(tracker.location(), "Expected target node after '->'");
        return false;
    }

    skip_whitespace_and_comments_d2(tracker);

    String* edge_label = nullptr;
    if (!tracker.atEnd() && tracker.current() == ':') {
        tracker.advance(); // skip colon
        skip_whitespace_and_comments_d2(tracker);
        edge_label = parse_d2_label(ctx);
    }

    // create nodes if they don't exist
    Element* from_node = create_node_element(ctx.input(), first_id, nullptr);
    Element* to_node = create_node_element(ctx.input(), second_id->chars, nullptr);
    Element* edge = create_edge_element(ctx.input(), first_id, second_id->chars,
                                       edge_label ? edge_label->chars : nullptr);

    if (from_node && to_node && edge) {
        // Source locations distinguish repeated node declarations during normalization.
        graph_set_source_span(ctx, from_node, first_start, tracker.location());
        graph_set_source_span(ctx, to_node, second_start, tracker.location());
        graph_set_source_span(ctx, edge, first_start, tracker.location());
        add_node_to_graph(ctx.input(), graph, from_node);
        add_node_to_graph(ctx.input(), graph, to_node);
        add_edge_to_graph(ctx.input(), graph, edge);
    }

    return true;
}

// Parse node with attributes block: node: { ... }
static bool parse_d2_node_with_block(InputContext& ctx, Element* graph,
                                     const char* first_id, const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    if (tracker.atEnd() || tracker.current() != ':') {
        return false;
    }

    tracker.advance(); // skip colon

    skip_whitespace_and_comments_d2(tracker);

    Element* node = create_node_element(ctx.input(), first_id, nullptr);
    if (node) {
        graph_set_source_span(ctx, node, source_start, tracker.location());
        add_node_to_graph(ctx.input(), graph, node);

        if (!tracker.atEnd() && tracker.current() == '{') {
            parse_d2_style_block(ctx, node);
        } else {
            String* label = parse_d2_label(ctx);
            if (label) add_graph_attribute(ctx.input(), node, "label", label->chars);
        }
    }

    return true;
}

void parse_graph_d2(Input* input, const char* d2_string) {
    if (!d2_string || !*d2_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, d2_string);
    SourceTracker& tracker = ctx.tracker;

    // create the main graph element
    Element* graph = create_graph_element(input, "directed", "hierarchical", "d2");
    if (!graph) {
        ctx.addError(SourceLocation(), "Failed to create graph element");
        return;
    }
    add_graph_attribute(input, graph, "ir-stage", "source");

    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_d2(tracker);

        if (tracker.atEnd()) break;

        // parse node/edge statement
        SourceLocation first_start = tracker.location();
        String* first_id = parse_d2_identifier(ctx);
        if (!first_id) {
            ctx.addError(tracker.location(), "Expected identifier");
            // skip to next line to recover
            skip_to_eol(tracker);
            if (!tracker.atEnd()) tracker.advance();

            if (ctx.shouldStopParsing()) break;
            continue;
        }

        skip_whitespace_and_comments_d2(tracker);

        // try different D2 statement types
        if (!tracker.atEnd()) {
            if (tracker.current() == ':' && d2_property_separator(first_id->chars)) {
                // node property assignment: node.property: value
                parse_d2_property_assignment(ctx, graph, first_id->chars, first_start);
            } else if (tracker.remaining() >= 2 && tracker.current() == '-' && tracker.peek(1) == '>') {
                // edge: node1 -> node2 [: label]
                parse_d2_edge(ctx, graph, first_id->chars, first_start);
            } else if (tracker.current() == ':') {
                // node with attributes block: node: { ... }
                parse_d2_node_with_block(ctx, graph, first_id->chars, first_start);
            } else {
                // simple node declaration
                Element* node = create_node_element(input, first_id->chars, nullptr);
                if (node) {
                    graph_set_source_span(ctx, node, first_start, tracker.location());
                    add_node_to_graph(input, graph, node);
                }
            }
        }

        // skip to next line
        skip_to_eol(tracker);
        if (!tracker.atEnd()) tracker.advance();

        if (ctx.shouldStopParsing()) break;
    }

    // set result
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
    input->root = {.element = graph};
}
