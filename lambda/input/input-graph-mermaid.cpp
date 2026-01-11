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

// Forward declarations for Mermaid parsing
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker);
static String* parse_mermaid_identifier(InputContext& ctx);
static String* parse_mermaid_label(InputContext& ctx);
static void parse_mermaid_node_def(InputContext& ctx, Element* graph);
static void parse_mermaid_edge_def(InputContext& ctx, Element* graph, String* from_id);
static void parse_mermaid_class_def(InputContext& ctx, Element* graph);
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id);

// Skip whitespace and comments in Mermaid
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker) {
    while (!tracker.atEnd()) {
        // skip whitespace
        if (isspace(tracker.current())) {
            tracker.advance();
            continue;
        }

        // skip %% comments
        if (tracker.current() == '%' && tracker.peek(1) == '%') {
            skip_to_eol(tracker);
            continue;
        }

        break;
    }
}

// Parse Mermaid identifier (alphanumeric + underscore + dash)
static String* parse_mermaid_identifier(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) return nullptr;

    char c = tracker.current();
    if (!isalpha(c) && c != '_') {
        return nullptr;
    }

    const char* start = tracker.rest();
    size_t len = 0;

    while (!tracker.atEnd()) {
        c = tracker.current();
        if (isalnum(c) || c == '_' || c == '-') {
            tracker.advance();
            len++;
        } else {
            break;
        }
    }

    return ctx.builder.createString(start, len);
}

// Parse Mermaid node shape and extract label
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) {
        return ctx.builder.createString(node_id);
    }

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    char open_char = tracker.current();
    char close_char = 0;
    const char* shape = "box"; // default shape

    // determine shape based on opening character(s)
    if (open_char == '[') {
        close_char = ']';
        shape = "box";
        tracker.advance();
    } else if (open_char == '(' && tracker.peek(1) == '(') {
        close_char = ')';
        shape = "circle";
        tracker.advance(2);
        // look for closing ))
    } else if (open_char == '(') {
        close_char = ')';
        shape = "ellipse";
        tracker.advance();
    } else if (open_char == '{') {
        close_char = '}';
        shape = "diamond";
        tracker.advance();
    } else if (open_char == '>') {
        close_char = ']';
        shape = "pentagon";
        tracker.advance();
    } else {
        // no shape specified, return node_id as label
        return ctx.builder.createString(node_id);
    }

    // Extract label text
    while (!tracker.atEnd() && tracker.current() != close_char) {
        char c = tracker.current();

        if (c == '\\') {
            tracker.advance();
            if (!tracker.atEnd()) {
                stringbuf_append_char(sb, tracker.current());
                tracker.advance();
            }
        } else {
            stringbuf_append_char(sb, c);
            tracker.advance();
        }
    }

    if (tracker.atEnd() || tracker.current() != close_char) {
        std::string msg = std::string("Expected closing character '") + close_char + "' for node shape";
        ctx.addError(tracker.location(), msg);
        return ctx.builder.createString(node_id);
    }

    tracker.advance(); // skip close_char

    // for shapes with double closing chars (e.g., circle with ))
    if (shape[0] == 'c' && tracker.current() == close_char) {
        tracker.advance();
    }

    const char* label_text = sb->str->chars;
    return ctx.builder.createString(label_text);
}

// Parse Mermaid label in quotes or brackets
static String* parse_mermaid_label(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) return nullptr;

    char quote = tracker.current();
    if (quote != '"' && quote != '\'' && quote != '[') {
        return nullptr;
    }

    char closing = (quote == '[') ? ']' : quote;
    tracker.advance();

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (!tracker.atEnd() && tracker.current() != closing) {
        char c = tracker.current();

        if (c == '\\') {
            tracker.advance();
            if (!tracker.atEnd()) {
                stringbuf_append_char(sb, tracker.current());
                tracker.advance();
            }
        } else {
            stringbuf_append_char(sb, c);
            tracker.advance();
        }
    }

    if (tracker.atEnd()) {
        std::string msg = std::string("Unterminated label, expected closing '") + closing + "'";
        ctx.addError(tracker.location(), msg);
        return nullptr;
    }

    tracker.advance(); // skip closing quote
    return ctx.builder.createString(sb->str->chars);
}

// Parse Mermaid node definition: nodeId[label] or nodeId(label) etc.
static void parse_mermaid_node_def(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // parse node ID
    String* node_id = parse_mermaid_identifier(ctx);
    if (!node_id) {
        return;
    }

    skip_whitespace_and_comments_mermaid(tracker);

    // parse node shape and label
    String* label = parse_mermaid_node_shape(ctx, node_id->chars);
    if (!label) {
        label = node_id; // use ID as label if no shape specified
    }

    // create node element
    Element* node = create_node_element(ctx.input(), node_id->chars, label->chars);

    // add to graph
    add_node_to_graph(ctx.input(), graph, node);
}

// Parse Mermaid edge definition: nodeA --> nodeB or nodeA -->|label| nodeB etc.
static void parse_mermaid_edge_def(InputContext& ctx, Element* graph, String* from_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // parse edge arrow type
    bool is_directed = true;
    String* label = nullptr;
    const char* edge_type = "solid";

    // check for edge arrow patterns: -->, --->, .-->, -.->
    if (tracker.current() == '-' || tracker.current() == '.') {
        if (tracker.current() == '.') {
            edge_type = "dashed";
            tracker.advance();
        }

        // skip dashes
        while (!tracker.atEnd() && tracker.current() == '-') {
            tracker.advance();
        }

        // check for arrow head
        if (tracker.current() == '>') {
            tracker.advance();
            is_directed = true;
        } else {
            is_directed = false;
        }
        
        // check for label inside |text| AFTER arrow head (Mermaid syntax: -->|label| target)
        if (tracker.current() == '|') {
            tracker.advance();
            StringBuf* sb = ctx.sb;
            stringbuf_reset(sb);

            while (!tracker.atEnd() && tracker.current() != '|') {
                stringbuf_append_char(sb, tracker.current());
                tracker.advance();
            }

            if (!tracker.atEnd() && tracker.current() == '|') {
                tracker.advance();
                label = ctx.builder.createString(sb->str->chars);
            }
        }
    } else {
        std::string msg = "Invalid edge syntax, expected edge arrow like '-->', '--->', or '-.->'";;
        ctx.addError(tracker.location(), msg);
        return;
    }

    skip_whitespace_and_comments_mermaid(tracker);

    // parse target node
    String* to_id = parse_mermaid_identifier(ctx);
    if (!to_id) {
        ctx.addError(tracker.location(), "Expected target node identifier");
        return;
    }
    
    // check if target node has a shape and create node if so
    skip_whitespace_and_comments_mermaid(tracker);
    if (tracker.current() == '[' || tracker.current() == '(' ||
        tracker.current() == '{' || tracker.current() == '>') {
        // determine shape from opening char
        char open_char = tracker.current();
        const char* node_shape = nullptr;
        if (open_char == '[') node_shape = "box";
        else if (open_char == '(') node_shape = "ellipse";
        else if (open_char == '{') node_shape = "diamond";
        else if (open_char == '>') node_shape = "pentagon";
        
        // parse node shape and get label
        String* to_label = parse_mermaid_node_shape(ctx, to_id->chars);
        
        // create and add target node element
        Element* to_node = create_node_element(ctx.input(), to_id->chars, 
            to_label ? to_label->chars : to_id->chars);
        if (node_shape) {
            add_graph_attribute(ctx.input(), to_node, "shape", node_shape);
        }
        add_node_to_graph(ctx.input(), graph, to_node);
    }

    // create edge element
    Element* edge = create_edge_element(ctx.input(), from_id->chars, to_id->chars,
                                        label ? label->chars : nullptr);

    // add edge type attribute if dashed
    if (strcmp(edge_type, "dashed") == 0) {
        add_graph_attribute(ctx.input(), edge, "style", "dashed");
    }

    // add to graph
    add_edge_to_graph(ctx.input(), graph, edge);
}

// Parse Mermaid class definition (for styling): class nodeIds className
static void parse_mermaid_class_def(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // parse node IDs (can be comma-separated)
    while (!tracker.atEnd() && !isspace(tracker.current())) {
        String* node_id = parse_mermaid_identifier(ctx);
        if (!node_id) break;

        skip_whitespace_and_comments_mermaid(tracker);

        // skip comma if present
        if (tracker.current() == ',') {
            tracker.advance();
            skip_whitespace_and_comments_mermaid(tracker);
        } else {
            break;
        }
    }

    skip_whitespace_and_comments_mermaid(tracker);

    // parse class name
    String* class_name = parse_mermaid_identifier(ctx);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name in class definition");
    }

    // Note: For now we just parse and discard class definitions
    // Full implementation would apply styling to matching nodes
}

// Main Mermaid parser function
void parse_graph_mermaid(Input* input, const char* mermaid_string) {
    InputContext ctx(input, mermaid_string, strlen(mermaid_string));

    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // detect diagram type
    String* diagram_type = nullptr;

    if (tracker.match("graph")) {
        diagram_type = ctx.builder.createString("flowchart");
        tracker.advance(5);

        // check for direction
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.match("TD") || tracker.match("TB") ||
            tracker.match("LR") || tracker.match("RL") ||
            tracker.match("BT")) {
            tracker.advance(2);
        }
    } else if (tracker.match("flowchart")) {
        diagram_type = ctx.builder.createString("flowchart");
        tracker.advance(9);

        // check for direction
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.match("TD") || tracker.match("TB") || tracker.match("LR")) {
            tracker.advance(2);
        }
    } else if (tracker.match("sequenceDiagram")) {
        diagram_type = ctx.builder.createString("sequence");
        tracker.advance(15);
    } else {
        // default to flowchart
        diagram_type = ctx.builder.createString("flowchart");
    }

    // create main graph element
    Element* graph = create_graph_element(input, "directed", "mermaid", "mermaid");
    add_graph_attribute(input, graph, "diagram-type", diagram_type->chars);
    add_graph_attribute(input, graph, "directed", "true");

    // parse diagram content
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);

        if (tracker.atEnd()) {
            break;
        }

        const char* line_start = tracker.rest();

        // check for classDef
        if (tracker.match("classDef")) {
            tracker.advance(8);
            parse_mermaid_class_def(ctx, graph);
            continue;
        }

        // try to parse as edge (look for edge operators)
        // size_t checkpoint = tracker.offset();
        String* potential_node = parse_mermaid_identifier(ctx);

        if (potential_node) {
            // check if node has shape and create node if so
            skip_whitespace_and_comments_mermaid(tracker);
            String* node_label = nullptr;
            const char* node_shape = nullptr;
            
            if (tracker.current() == '[' || tracker.current() == '(' ||
                tracker.current() == '{' || tracker.current() == '>') {
                // determine shape from opening char
                char open_char = tracker.current();
                if (open_char == '[') node_shape = "box";
                else if (open_char == '(') node_shape = "ellipse";
                else if (open_char == '{') node_shape = "diamond";
                else if (open_char == '>') node_shape = "pentagon";
                
                // parse node shape and get label
                node_label = parse_mermaid_node_shape(ctx, potential_node->chars);
                skip_whitespace_and_comments_mermaid(tracker);
                
                // create and add node element
                Element* node = create_node_element(ctx.input(), potential_node->chars, 
                    node_label ? node_label->chars : potential_node->chars);
                if (node_shape) {
                    add_graph_attribute(ctx.input(), node, "shape", node_shape);
                }
                add_node_to_graph(ctx.input(), graph, node);
            }

            // look for edge operator
            if (tracker.match("-->") || tracker.match("-.->") ||
                tracker.match("==>") || tracker.match("---") ||
                tracker.match("-.-") || tracker.match("===")) {
                // this is an edge
                parse_mermaid_edge_def(ctx, graph, potential_node);
            } else if (!node_label) {
                // this is a standalone node without shape
                parse_mermaid_node_def(ctx, graph);
            }
        }

        // skip to next line if we haven't made progress
        const char* line_end = tracker.rest();
        if (line_end == line_start) {
            skip_to_eol(tracker);
            if (!tracker.atEnd() && tracker.current() == '\n') {
                tracker.advance();
            }
        }
    }

    // set graph as root
    input->root = {.element = graph};

    // report errors if any
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
