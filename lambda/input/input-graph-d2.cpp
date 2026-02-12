#include "input-graph.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <ctype.h>

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
static bool parse_d2_property_assignment(InputContext& ctx, Element* graph, const char* first_id);
static bool parse_d2_edge(InputContext& ctx, Element* graph, const char* first_id);
static bool parse_d2_node_with_block(InputContext& ctx, Element* graph, const char* first_id);

// Helper: skip to end of line
static void skip_to_eol(SourceTracker& tracker) {
    while (!tracker.atEnd() && tracker.current() != '\n') {
        tracker.advance();
    }
}

// Skip whitespace and comments in D2
static void skip_whitespace_and_comments_d2(SourceTracker& tracker) {
    while (!tracker.atEnd()) {
        char c = tracker.current();

        // skip whitespace
        if (isspace(c)) {
            tracker.advance();
            continue;
        }

        // skip # comments
        if (c == '#') {
            skip_to_eol(tracker);
            continue;
        }

        break;
    }
}

// Parse D2 identifier (alphanumeric + underscore + hyphen + dot)
static String* parse_d2_identifier(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

    skip_whitespace_and_comments_d2(tracker);

    if (tracker.atEnd()) return nullptr;

    const char* start = tracker.rest();
    size_t len = 0;

    while (!tracker.atEnd()) {
        char c = tracker.current();
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            tracker.advance();
            len++;
        } else {
            break;
        }
    }

    if (len == 0) return nullptr;

    return ctx.builder.createString(start, len);
}

// Parse D2 quoted string
static String* parse_d2_quoted_string(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;

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

            char escaped = tracker.current();
            switch (escaped) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                default:
                    stringbuf_append_char(sb, '\\');
                    stringbuf_append_char(sb, escaped);
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
        if (c == '\n' || c == '\r' || c == '{' || c == '}' || c == '#') {
            break;
        }
        tracker.advance();
        len++;
    }

    // trim trailing whitespace
    while (len > 0 && isspace(start[len - 1])) {
        len--;
    }

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

        String* value = parse_d2_label(ctx);
        if (value) {
            // convert D2 style properties to CSS-aligned attributes
            const char* css_property = property->chars;
            if (strcmp(property->chars, "fill") == 0) {
                css_property = "background-color";
            } else if (strcmp(property->chars, "stroke") == 0) {
                css_property = "border-color";
            } else if (strcmp(property->chars, "stroke-width") == 0) {
                css_property = "border-width";
            } else if (strcmp(property->chars, "stroke-dash") == 0) {
                css_property = "stroke-dasharray";
            }

            add_graph_attribute(ctx.input(), element, css_property, value->chars);
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
                                         Element* graph, const char* first_id) {
    SourceTracker& tracker = ctx.tracker;

    if (tracker.atEnd() || tracker.current() != '.') {
        return false;
    }

    tracker.advance(); // skip dot

    String* property = parse_d2_identifier(ctx);
    if (!property) {
        ctx.addError(tracker.location(), "Expected property name after '.'");
        return false;
    }

    skip_whitespace_and_comments_d2(tracker);

    if (tracker.atEnd() || tracker.current() != ':') {
        ctx.addError(tracker.location(), "Expected ':' after property name");
        return false;
    }

    tracker.advance(); // skip colon

    skip_whitespace_and_comments_d2(tracker);

    // find or create the node
    Element* node = create_node_element(ctx.input(), first_id, nullptr);
    if (node) {
        add_node_to_graph(ctx.input(), graph, node);

        if (!tracker.atEnd() && tracker.peek() == '{') {
            // style block
            parse_d2_style_block(ctx, node);
        } else {
            // single property value
            String* value = parse_d2_label(ctx);
            if (value) {
                // convert D2 properties to CSS-aligned attributes
                const char* css_property = property->chars;
                if (strcmp(property->chars, "shape") == 0) {
                    css_property = "shape";
                } else if (strcmp(property->chars, "label") == 0) {
                    css_property = "label";
                } else if (strcmp(property->chars, "style") == 0) {
                    css_property = "style";
                }

                add_graph_attribute(ctx.input(), node, css_property, value->chars);
            }
        }
    }

    return true;
}

// Parse edge: node1 -> node2 [: label]
static bool parse_d2_edge(InputContext& ctx,
                          Element* graph, const char* first_id) {
    SourceTracker& tracker = ctx.tracker;

    if (tracker.remaining() < 2 || tracker.current() != '-' || tracker.peek(1) != '>') {
        return false;
    }

    tracker.advance(); // skip -
    tracker.advance(); // skip >

    skip_whitespace_and_comments_d2(tracker);

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
        add_node_to_graph(ctx.input(), graph, from_node);
        add_node_to_graph(ctx.input(), graph, to_node);
        add_edge_to_graph(ctx.input(), graph, edge);
    }

    return true;
}

// Parse node with attributes block: node: { ... }
static bool parse_d2_node_with_block(InputContext& ctx, Element* graph, const char* first_id) {
    SourceTracker& tracker = ctx.tracker;
    if (tracker.atEnd() || tracker.peek() != ':') {
        return false;
    }

    tracker.advance(); // skip colon

    skip_whitespace_and_comments_d2(tracker);

    Element* node = create_node_element(ctx.input(), first_id, nullptr);
    if (node) {
        add_node_to_graph(ctx.input(), graph, node);

        if (!tracker.atEnd() && tracker.peek() == '{') {
            parse_d2_style_block(ctx, node);
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

    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_d2(tracker);

        if (tracker.atEnd()) break;

        // parse node/edge statement
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

        bool parsed = false;

        // try different D2 statement types
        if (!tracker.atEnd()) {
            if (tracker.current() == '.') {
                // node property assignment: node.property: value
                parsed = parse_d2_property_assignment(ctx, graph, first_id->chars);
            } else if (tracker.remaining() >= 2 && tracker.current() == '-' && tracker.peek(1) == '>') {
                // edge: node1 -> node2 [: label]
                parsed = parse_d2_edge(ctx, graph, first_id->chars);
            } else if (tracker.current() == ':') {
                // node with attributes block: node: { ... }
                parsed = parse_d2_node_with_block(ctx, graph, first_id->chars);
            } else {
                // simple node declaration
                Element* node = create_node_element(input, first_id->chars, nullptr);
                if (node) {
                    add_node_to_graph(input, graph, node);
                }
                parsed = true;
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
