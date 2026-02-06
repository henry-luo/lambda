#include "input-graph.h"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../../lib/log.h"
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
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph);
static void parse_mermaid_subgraph_content(InputContext& ctx, Element* subgraph_elem);

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
// Supports all 12 Mermaid flowchart shapes:
//   [text]       - rectangle (box)
//   (text)       - rounded rectangle
//   ((text))     - circle
//   (((text)))   - double circle
//   {text}       - diamond (rhombus)
//   {{text}}     - hexagon
//   ([text])     - stadium (pill shape)
//   [(text)]     - cylinder (database)
//   [[text]]     - subroutine (double-bordered rectangle)
//   >text]       - asymmetric (flag/banner)
//   [/text\]     - trapezoid (wider bottom)
//   [\text/]     - inverse trapezoid (wider top)
//
// Returns the extracted label text and sets g_current_node_shape for the caller
static const char* g_current_node_shape = "box";

static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) {
        g_current_node_shape = "box";
        return ctx.builder.createString(node_id);
    }

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    char c0 = tracker.current();
    char c1 = tracker.peek(1);
    char c2 = tracker.peek(2);

    const char* shape = "box";
    const char* close_pattern = nullptr;  // for complex closings
    char close_char = 0;
    int open_len = 1;

    // Detect shape based on opening delimiter pattern
    // Order matters - check longer patterns first

    if (c0 == '(' && c1 == '(' && c2 == '(') {
        // (((text))) - double circle
        shape = "doublecircle";
        close_pattern = ")))";
        open_len = 3;
    } else if (c0 == '(' && c1 == '(') {
        // ((text)) - circle
        shape = "circle";
        close_pattern = "))";
        open_len = 2;
    } else if (c0 == '(' && c1 == '[') {
        // ([text]) - stadium
        shape = "stadium";
        close_pattern = "])";
        open_len = 2;
    } else if (c0 == '[' && c1 == '(') {
        // [(text)] - cylinder
        shape = "cylinder";
        close_pattern = ")]";
        open_len = 2;
    } else if (c0 == '[' && c1 == '[') {
        // [[text]] - subroutine
        shape = "subroutine";
        close_pattern = "]]";
        open_len = 2;
    } else if (c0 == '{' && c1 == '{') {
        // {{text}} - hexagon
        shape = "hexagon";
        close_pattern = "}}";
        open_len = 2;
    } else if (c0 == '[' && c1 == '/') {
        // [/text\] - trapezoid
        shape = "trapezoid";
        close_pattern = "\\]";
        open_len = 2;
    } else if (c0 == '[' && c1 == '\\') {
        // [\text/] - inverse trapezoid
        shape = "trapezoid-alt";
        close_pattern = "/]";
        open_len = 2;
    } else if (c0 == '>') {
        // >text] - asymmetric
        shape = "asymmetric";
        close_char = ']';
        open_len = 1;
    } else if (c0 == '(') {
        // (text) - rounded rectangle
        shape = "rounded";
        close_char = ')';
        open_len = 1;
    } else if (c0 == '[') {
        // [text] - rectangle (default box)
        shape = "box";
        close_char = ']';
        open_len = 1;
    } else if (c0 == '{') {
        // {text} - diamond
        shape = "diamond";
        close_char = '}';
        open_len = 1;
    } else {
        // no shape specified, return node_id as label
        g_current_node_shape = "box";
        return ctx.builder.createString(node_id);
    }

    // Advance past opening delimiter
    tracker.advance(open_len);

    g_current_node_shape = shape;

    // Extract label text until closing delimiter
    if (close_pattern) {
        size_t close_len = strlen(close_pattern);
        while (!tracker.atEnd()) {
            // Check for closing pattern
            bool found_close = true;
            for (size_t i = 0; i < close_len; i++) {
                if (tracker.peek(i) != close_pattern[i]) {
                    found_close = false;
                    break;
                }
            }
            if (found_close) {
                tracker.advance(close_len);
                break;
            }

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
    } else {
        // Simple single-character closing
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

        if (!tracker.atEnd() && tracker.current() == close_char) {
            tracker.advance();
        }
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
        ctx.addError(tracker.location(), "Unterminated label, expected closing '%c'", closing);
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

    // parse node shape and label (sets g_current_node_shape)
    String* label = parse_mermaid_node_shape(ctx, node_id->chars);
    if (!label) {
        label = node_id; // use ID as label if no shape specified
    }

    // create node element with shape passed directly (before finalization)
    Element* node = create_node_element(ctx.input(), node_id->chars, label->chars, g_current_node_shape);

    // add to graph
    add_node_to_graph(ctx.input(), graph, node);
}

// Parse Mermaid edge definition: nodeA --> nodeB or nodeA -->|label| nodeB etc.
// Supports all Mermaid edge styles:
//   -->   solid arrow
//   --->  solid arrow (longer)
//   -.->  dotted arrow
//   ==>   thick arrow
//   ---   solid line (no arrow)
//   -.-   dotted line (no arrow)
//   ===   thick line (no arrow)
//   <-->  bidirectional solid
//   <-.-> bidirectional dotted
//   <==>  bidirectional thick
static void parse_mermaid_edge_def(InputContext& ctx, Element* graph, String* from_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // Edge properties
    bool has_arrow_start = false;
    bool has_arrow_end = false;
    String* label = nullptr;
    const char* edge_style = "solid";

    // Check for starting arrow (bidirectional)
    if (tracker.current() == '<') {
        has_arrow_start = true;
        tracker.advance();
    }

    // Determine edge style from first character
    char first_char = tracker.current();
    if (first_char == '=') {
        edge_style = "thick";
    } else if (first_char == '.') {
        edge_style = "dotted";
        tracker.advance();
        // After '.', expect '-' for dashed pattern
        if (tracker.current() != '-') {
            ctx.addError(tracker.location(), "Invalid edge syntax after '.'");
            return;
        }
    } else if (first_char == '-') {
        // Could be solid or dotted (-.->)
        // Check if this is part of -.- pattern
    } else {
        ctx.addError(tracker.location(), "Invalid edge syntax, expected '-', '=', or '.'");
        return;
    }

    // Skip main line characters (-, =, or . combinations)
    if (edge_style[0] == 't') {  // thick
        while (!tracker.atEnd() && tracker.current() == '=') {
            tracker.advance();
        }
    } else {
        // solid or dotted - skip dashes and dots
        while (!tracker.atEnd() && (tracker.current() == '-' || tracker.current() == '.')) {
            if (tracker.current() == '.') {
                edge_style = "dotted";
            }
            tracker.advance();
        }
    }

    // Check for ending arrow
    if (tracker.current() == '>') {
        has_arrow_end = true;
        tracker.advance();
    }

    // Check for label inside |text| AFTER arrow head (Mermaid syntax: -->|label| target)
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

    skip_whitespace_and_comments_mermaid(tracker);

    // parse target node
    String* to_id = parse_mermaid_identifier(ctx);
    if (!to_id) {
        ctx.addError(tracker.location(), "Expected target node identifier");
        return;
    }

    // check if target node has a shape and create node if so
    skip_whitespace_and_comments_mermaid(tracker);
    char c0 = tracker.current();
    char c1 = tracker.peek(1);

    // Check for any shape opening pattern
    bool has_shape = (c0 == '[') || (c0 == '(') || (c0 == '{') || (c0 == '>');

    if (has_shape) {
        // parse node shape and get label
        String* to_label = parse_mermaid_node_shape(ctx, to_id->chars);

        // create and add target node element with shape passed directly (before finalization)
        Element* to_node = create_node_element(ctx.input(), to_id->chars,
            to_label ? to_label->chars : to_id->chars, g_current_node_shape);
        add_node_to_graph(ctx.input(), graph, to_node);
    }

    // create edge element with all attributes passed directly (before finalization)
    Element* edge = create_edge_element(ctx.input(), from_id->chars, to_id->chars,
                                        label ? label->chars : nullptr,
                                        edge_style,
                                        has_arrow_start ? "true" : "false",
                                        has_arrow_end ? "true" : "false");

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

// Parse Mermaid subgraph: subgraph id [label] ... end
// Supports direction override: direction LR/TB/BT/RL
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // "subgraph" keyword already consumed by caller
    // Parse subgraph ID
    String* subgraph_id = parse_mermaid_identifier(ctx);
    if (!subgraph_id) {
        // Generate a unique ID
        static int subgraph_counter = 0;
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "subgraph_%d", subgraph_counter++);
        subgraph_id = ctx.builder.createString(id_buf);
    }

    // Don't skip all whitespace here - just skip spaces on the same line
    // to check for optional [Label]
    while (!tracker.atEnd() && (tracker.current() == ' ' || tracker.current() == '\t')) {
        tracker.advance();
    }

    // Check for optional label in brackets: subgraph id [Label Text]
    String* label = nullptr;
    if (tracker.current() == '[') {
        tracker.advance();
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        while (!tracker.atEnd() && tracker.current() != ']') {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
        if (!tracker.atEnd() && tracker.current() == ']') {
            tracker.advance();
        }
        label = ctx.builder.createString(sb->str->chars);
    }

    // Skip to end of line - any remaining content is just comments or whitespace
    // The label is either in brackets [Label] or we use the subgraph ID
    while (!tracker.atEnd() && tracker.current() != '\n') {
        tracker.advance();
    }
    if (!tracker.atEnd() && tracker.current() == '\n') {
        tracker.advance();
    }

    // Create subgraph element - use label if provided, otherwise use ID as label
    Element* subgraph_elem = create_cluster_element(ctx.input(), subgraph_id->chars,
        label ? label->chars : subgraph_id->chars);

    // Parse subgraph content until "end" keyword
    parse_mermaid_subgraph_content(ctx, subgraph_elem);

    // Add subgraph to parent graph
    add_node_to_graph(ctx.input(), parent_graph, subgraph_elem);
}

// Parse content inside a subgraph (nodes, edges, nested subgraphs, direction)
static void parse_mermaid_subgraph_content(InputContext& ctx, Element* subgraph_elem) {
    SourceTracker& tracker = ctx.tracker;

    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);

        if (tracker.atEnd()) break;

        // Check for "end" keyword
        if (tracker.match("end")) {
            // Verify it's "end" and not "endpoint" or similar
            char next = tracker.peek(3);
            if (!isalnum(next) && next != '_') {
                tracker.advance(3);
                break;
            }
        }

        // Check for direction override: direction LR/TB/BT/RL
        if (tracker.match("direction")) {
            tracker.advance(9);
            skip_whitespace_and_comments_mermaid(tracker);

            // Parse direction value
            if (tracker.match("LR") || tracker.match("RL") ||
                tracker.match("TB") || tracker.match("TD") ||
                tracker.match("BT")) {
                char dir_buf[3] = {tracker.current(), tracker.peek(1), '\0'};
                tracker.advance(2);

                // Add direction attribute to subgraph
                add_graph_attribute(ctx.input(), subgraph_elem, "direction", dir_buf);
            }
            continue;
        }

        // Check for nested subgraph
        if (tracker.match("subgraph")) {
            tracker.advance(8);
            parse_mermaid_subgraph(ctx, subgraph_elem);
            continue;
        }

        const char* line_start = tracker.rest();

        // Try to parse node or edge (same logic as main parser)
        String* potential_node = parse_mermaid_identifier(ctx);

        if (potential_node) {
            // Check if node has shape
            skip_whitespace_and_comments_mermaid(tracker);
            String* node_label = nullptr;

            char c0 = tracker.current();
            if (c0 == '[' || c0 == '(' || c0 == '{' || c0 == '>') {
                node_label = parse_mermaid_node_shape(ctx, potential_node->chars);
                skip_whitespace_and_comments_mermaid(tracker);

                Element* node = create_node_element(ctx.input(), potential_node->chars,
                    node_label ? node_label->chars : potential_node->chars, g_current_node_shape);
                add_node_to_graph(ctx.input(), subgraph_elem, node);
            }

            // Check for edge operator
            bool is_edge = false;
            if (tracker.current() == '<') {
                is_edge = true;
            } else if (tracker.match("-->") || tracker.match("-.->") ||
                tracker.match("==>") || tracker.match("---") ||
                tracker.match("-.-") || tracker.match("===") ||
                tracker.match("--") || tracker.current() == '-' || tracker.current() == '=') {
                is_edge = true;
            }

            if (is_edge) {
                parse_mermaid_edge_def(ctx, subgraph_elem, potential_node);
            } else if (!node_label) {
                // Standalone node without shape - create it
                Element* node = create_node_element(ctx.input(), potential_node->chars,
                    potential_node->chars, "box");
                add_node_to_graph(ctx.input(), subgraph_elem, node);
            }
        }

        // Skip to next line if no progress
        const char* line_end = tracker.rest();
        if (line_end == line_start) {
            skip_to_eol(tracker);
            if (!tracker.atEnd() && tracker.current() == '\n') {
                tracker.advance();
            }
        }
    }
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

        // check for subgraph
        if (tracker.match("subgraph")) {
            tracker.advance(8);
            parse_mermaid_subgraph(ctx, graph);
            continue;
        }

        // try to parse as edge (look for edge operators)
        // size_t checkpoint = tracker.offset();
        String* potential_node = parse_mermaid_identifier(ctx);

        if (potential_node) {
            // check if node has shape and create node if so
            skip_whitespace_and_comments_mermaid(tracker);
            String* node_label = nullptr;

            // Check for any shape opening pattern
            char c0 = tracker.current();
            if (c0 == '[' || c0 == '(' || c0 == '{' || c0 == '>') {
                // parse node shape and get label (sets g_current_node_shape)
                node_label = parse_mermaid_node_shape(ctx, potential_node->chars);
                skip_whitespace_and_comments_mermaid(tracker);

                // create and add node element with shape passed directly (before finalization)
                Element* node = create_node_element(ctx.input(), potential_node->chars,
                    node_label ? node_label->chars : potential_node->chars, g_current_node_shape);
                add_node_to_graph(ctx.input(), graph, node);
            }

            // look for edge operator (including bidirectional <-->)
            bool is_edge = false;
            if (tracker.current() == '<') {
                // could be bidirectional: <-->, <-.->, <==>
                is_edge = true;
            } else if (tracker.match("-->") || tracker.match("-.->") ||
                tracker.match("==>") || tracker.match("---") ||
                tracker.match("-.-") || tracker.match("===") ||
                tracker.match("--") || tracker.match("-.-") ||
                tracker.current() == '-' || tracker.current() == '=') {
                is_edge = true;
            }

            if (is_edge) {
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
