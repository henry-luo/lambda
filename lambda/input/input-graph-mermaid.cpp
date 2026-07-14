#include "input-graph.h"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include "../../lib/log.h"
#include <string.h>

using namespace lambda;

// Forward declarations for Mermaid parsing
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker);
static String* parse_mermaid_identifier(InputContext& ctx);
static void parse_mermaid_edge_def(InputContext& ctx, Element* graph, Element* root_graph,
                                    String* from_id);
static void parse_mermaid_class_def(InputContext& ctx, Element* graph);
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id, const char** out_shape);
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph, Element* root_graph);
static void parse_mermaid_subgraph_content(InputContext& ctx, Element* subgraph_elem,
                                            Element* root_graph);

static void skip_inline_whitespace(SourceTracker& tracker) {
    while (!tracker.atEnd() && (tracker.current() == ' ' || tracker.current() == '\t' ||
           tracker.current() == '\r')) {
        tracker.advance();
    }
}

static bool consume_keyword(SourceTracker& tracker, const char* keyword) {
    size_t length = strlen(keyword);
    if (!tracker.match(keyword)) return false;
    char next = tracker.peek(length);
    if (str_char_is_alnum(next) || next == '_' || next == '-') return false;
    tracker.advance(length);
    return true;
}

static const char* normalize_direction(const char direction[3]) {
    return strcmp(direction, "TD") == 0 ? "TB" : direction;
}

static bool parse_mermaid_direction(SourceTracker& tracker, char direction[3]) {
    skip_inline_whitespace(tracker);
    if (!(tracker.match("LR") || tracker.match("RL") || tracker.match("TB") ||
          tracker.match("TD") || tracker.match("BT"))) {
        return false;
    }
    direction[0] = tracker.current();
    direction[1] = tracker.peek(1);
    direction[2] = '\0';
    tracker.advance(2);
    return true;
}

static Element* find_direct_node(Element* graph, const char* id) {
    if (!graph || !id) return nullptr;
    ElementReader graph_reader(graph);
    ElementReader child;
    auto children = graph_reader.childElements();
    while (children.next(&child)) {
        if (!child.hasTag("node")) continue;
        const char* child_id = child.get_attr_string("id");
        if (child_id && strcmp(child_id, id) == 0) {
            return (Element*)child.element();
        }
    }
    return nullptr;
}

static Element* find_mermaid_node(Element* graph, const char* id) {
    Element* node = find_direct_node(graph, id);
    if (node) return node;

    ElementReader graph_reader(graph);
    ElementReader child;
    auto children = graph_reader.childElements();
    while (children.next(&child)) {
        if (!child.hasTag("subgraph")) continue;
        node = find_mermaid_node((Element*)child.element(), id);
        if (node) return node;
    }
    return nullptr;
}

static Element* ensure_mermaid_node(InputContext& ctx, Element* graph, Element* root_graph,
                                     const char* id) {
    Element* node = find_mermaid_node(graph, id);
    if (!node && graph != root_graph) node = find_mermaid_node(root_graph, id);
    if (node) return node;

    // Mermaid edges declare their endpoints implicitly, so the IR must materialize
    // both endpoints before layout can resolve edge identity and dimensions.
    node = create_node_element(ctx.input(), id, id, "box");
    add_node_to_graph(ctx.input(), graph, node);
    return node;
}

static Element* upsert_mermaid_node(InputContext& ctx, Element* graph, const char* id,
                                    Element* root_graph, const char* label,
                                    const char* shape) {
    Element* node = find_mermaid_node(graph, id);
    if (!node && graph != root_graph) node = find_mermaid_node(root_graph, id);
    if (!node) {
        node = create_node_element(ctx.input(), id, label, shape);
        add_node_to_graph(ctx.input(), graph, node);
        return node;
    }

    // Repeated declarations refine one semantic node; duplicate elements would
    // give layout ambiguous endpoint ownership and unstable measured dimensions.
    if (label) add_graph_attribute(ctx.input(), node, "label", label);
    if (shape) add_graph_attribute(ctx.input(), node, "shape", shape);
    return node;
}

static void add_mermaid_metadata(InputContext& ctx, Element* graph, const char* tag,
                                 const char* key1, const char* value1,
                                 const char* key2, const char* value2) {
    ElementBuilder builder = ctx.builder.element(tag);
    if (key1 && value1) builder.attr(key1, value1);
    if (key2 && value2) builder.attr(key2, value2);
    add_node_to_graph(ctx.input(), graph, builder.final().element);
}

// skip whitespace and %% line comments
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker) {
    skip_wsc(tracker, "%%", nullptr, false);
}

// read Mermaid identifier: [A-Za-z_][A-Za-z0-9_\-]* (whitespace skipped first)
static String* parse_mermaid_identifier(InputContext& ctx) {
    skip_whitespace_and_comments_mermaid(ctx.tracker);
    return read_graph_identifier(ctx, "-", true);
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
// Returns the extracted label text and sets *out_shape for the caller
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id, const char** out_shape) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) {
        *out_shape = "box";
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
        *out_shape = "box";
        return ctx.builder.createString(node_id);
    }

    // Advance past opening delimiter
    tracker.advance(open_len);

    *out_shape = shape;

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
    size_t label_length = sb->str->len;
    if (label_length >= 2 && label_text[0] == '"' && label_text[label_length - 1] == '"') {
        // Mermaid quotes delimit labels and are not part of the rendered text.
        return ctx.builder.createString(label_text + 1, label_length - 2);
    }
    return ctx.builder.createString(label_text, label_length);
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
static void parse_mermaid_edge_def(InputContext& ctx, Element* graph, Element* root_graph,
                                    String* from_id) {
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

    // Check for any shape opening pattern
    bool has_shape = (c0 == '[') || (c0 == '(') || (c0 == '{') || (c0 == '>');

    if (has_shape) {
        // parse node shape and get label
        const char* to_shape = "box";
        String* to_label = parse_mermaid_node_shape(ctx, to_id->chars, &to_shape);

        upsert_mermaid_node(ctx, graph, to_id->chars, root_graph,
            to_label ? to_label->chars : to_id->chars, to_shape);
    } else {
        ensure_mermaid_node(ctx, graph, root_graph, to_id->chars);
    }

    ensure_mermaid_node(ctx, graph, root_graph, from_id->chars);

    // create edge element with all attributes passed directly (before finalization)
    Element* edge = create_edge_element(ctx.input(), from_id->chars, to_id->chars,
                                        label ? label->chars : nullptr,
                                        edge_style,
                                        has_arrow_start ? "true" : "false",
                                        has_arrow_end ? "true" : "false");

    // add to graph
    add_edge_to_graph(ctx.input(), graph, edge);
}

// Parse Mermaid class assignment (for styling): class nodeIds className
static void parse_mermaid_class_def(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    StringBuf* targets = ctx.sb;
    stringbuf_reset(targets);

    // parse node IDs (can be comma-separated)
    while (!tracker.atEnd() && !str_char_is_ascii_space(tracker.current())) {
        String* node_id = parse_mermaid_identifier(ctx);
        if (!node_id) break;

        if (targets->str->len > 0) stringbuf_append_char(targets, ',');
        stringbuf_append_str_n(targets, node_id->chars, node_id->len);

        skip_inline_whitespace(tracker);

        // skip comma if present
        if (tracker.current() == ',') {
            tracker.advance();
            skip_inline_whitespace(tracker);
        } else {
            break;
        }
    }

    skip_inline_whitespace(tracker);

    // parse class name
    String* class_name = parse_mermaid_identifier(ctx);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name in class definition");
        return;
    }

    add_mermaid_metadata(ctx, graph, "class-assignment", "targets", targets->str->chars,
                         "class", class_name->chars);
}

// Parse Mermaid classDef into semantic style metadata without interpreting CSS here.
static void parse_mermaid_style_rule(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    String* class_name = parse_mermaid_identifier(ctx);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name after classDef");
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    const char* declarations_start = tracker.rest();
    while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
    size_t declarations_length = (size_t)(tracker.rest() - declarations_start);
    while (declarations_length > 0 &&
           (declarations_start[declarations_length - 1] == ';' ||
            declarations_start[declarations_length - 1] == '\r' ||
            declarations_start[declarations_length - 1] == ' ' ||
            declarations_start[declarations_length - 1] == '\t')) {
        declarations_length--;
    }
    String* declarations = ctx.builder.createString(declarations_start, declarations_length);
    add_mermaid_metadata(ctx, graph, "style-rule", "class", class_name->chars,
                         "declarations", declarations->chars);
}

// Parse Mermaid subgraph: subgraph id [label] ... end
// Supports direction override: direction LR/TB/BT/RL
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph, Element* root_graph) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    // "subgraph" keyword already consumed by caller
    // Parse subgraph ID
    String* subgraph_id = nullptr;
    String* quoted_label = nullptr;
    skip_inline_whitespace(tracker);
    if (tracker.current() == '"') {
        tracker.advance();
        const char* label_start = tracker.rest();
        while (!tracker.atEnd() && tracker.current() != '"' && tracker.current() != '\n') {
            tracker.advance();
        }
        size_t label_length = (size_t)(tracker.rest() - label_start);
        quoted_label = ctx.builder.createString(label_start, label_length);
        subgraph_id = quoted_label;
        if (tracker.current() == '"') tracker.advance();
    } else {
        subgraph_id = parse_mermaid_identifier(ctx);
    }
    if (!subgraph_id) {
        // Generate a unique ID using the tracker offset as a unique seed
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), "subgraph_%zu", ctx.tracker.offset());
        subgraph_id = ctx.builder.createString(id_buf);
    }

    // Don't skip all whitespace here - just skip spaces on the same line
    // to check for optional [Label]
    while (!tracker.atEnd() && (tracker.current() == ' ' || tracker.current() == '\t')) {
        tracker.advance();
    }

    // Check for optional label in brackets: subgraph id [Label Text]
    String* label = quoted_label;
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
    parse_mermaid_subgraph_content(ctx, subgraph_elem, root_graph);

    // Add subgraph to parent graph
    add_node_to_graph(ctx.input(), parent_graph, subgraph_elem);
}

// Parse content inside a subgraph (nodes, edges, nested subgraphs, direction)
static void parse_mermaid_subgraph_content(InputContext& ctx, Element* subgraph_elem,
                                            Element* root_graph) {
    SourceTracker& tracker = ctx.tracker;

    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);

        if (tracker.atEnd()) break;

        // Check for "end" keyword
        if (consume_keyword(tracker, "end")) {
            break;
        }

        // Check for direction override: direction LR/TB/BT/RL
        if (consume_keyword(tracker, "direction")) {
            char direction[3] = {0};
            if (parse_mermaid_direction(tracker, direction)) {
                add_graph_attribute(ctx.input(), subgraph_elem, "direction",
                                    normalize_direction(direction));
            }
            continue;
        }

        // Check for nested subgraph
        if (consume_keyword(tracker, "subgraph")) {
            parse_mermaid_subgraph(ctx, subgraph_elem, root_graph);
            continue;
        }

        if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, subgraph_elem);
            continue;
        }

        if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, subgraph_elem);
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
                const char* sub_node_shape = "box";
                node_label = parse_mermaid_node_shape(ctx, potential_node->chars, &sub_node_shape);
                skip_whitespace_and_comments_mermaid(tracker);

                upsert_mermaid_node(ctx, subgraph_elem, potential_node->chars, root_graph,
                    node_label ? node_label->chars : potential_node->chars, sub_node_shape);
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
                parse_mermaid_edge_def(ctx, subgraph_elem, root_graph, potential_node);
            } else if (!node_label) {
                // Standalone node without shape - create it
                ensure_mermaid_node(ctx, subgraph_elem, root_graph, potential_node->chars);
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
    if (!mermaid_string || !*mermaid_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }

    InputContext ctx(input, mermaid_string, strlen(mermaid_string));

    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    const char* diagram_type = "flowchart";
    char direction[3] = {'T', 'B', '\0'};
    bool unsupported_chart = false;

    if (consume_keyword(tracker, "graph") || consume_keyword(tracker, "flowchart")) {
        parse_mermaid_direction(tracker, direction);
    } else if (consume_keyword(tracker, "sequenceDiagram")) {
        diagram_type = "sequence";
        unsupported_chart = true;
    } else if (consume_keyword(tracker, "gantt")) {
        diagram_type = "gantt";
        unsupported_chart = true;
    } else if (consume_keyword(tracker, "pie")) {
        diagram_type = "pie";
        unsupported_chart = true;
    } else if (consume_keyword(tracker, "sankey-beta")) {
        diagram_type = "sankey";
        unsupported_chart = true;
    } else if (consume_keyword(tracker, "timeline")) {
        diagram_type = "timeline";
        unsupported_chart = true;
    } else if (consume_keyword(tracker, "xychart-beta") || consume_keyword(tracker, "xychart")) {
        diagram_type = "xychart";
        unsupported_chart = true;
    }

    // create main graph element
    Element* graph = create_graph_element(input, "directed", "mermaid", "mermaid");
    add_graph_attribute(input, graph, "version", "1");
    add_graph_attribute(input, graph, "kind", diagram_type);
    add_graph_attribute(input, graph, "diagram-type", diagram_type);
    add_graph_attribute(input, graph, "directed", "true");
    add_graph_attribute(input, graph, "direction", normalize_direction(direction));
    add_graph_attribute(input, graph, "rank-dir", normalize_direction(direction));

    if (unsupported_chart) {
        // Chart-oriented Mermaid families have different semantic models; treating
        // their statements as graph nodes silently corrupts the common Graph IR.
        add_graph_attribute(input, graph, "status", "unsupported");
        ctx.addError("Mermaid %s diagrams belong to lambda.package.chart", diagram_type);
        input->root = {.element = graph};
        ctx.logErrors();
        return;
    }

    // parse diagram content
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);

        if (tracker.atEnd()) {
            break;
        }

        const char* line_start = tracker.rest();

        // check for classDef
        if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph);
            continue;
        }

        // check for class assignments
        if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, graph);
            continue;
        }

        // check for subgraph
        if (consume_keyword(tracker, "subgraph")) {
            parse_mermaid_subgraph(ctx, graph, graph);
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
                // parse node shape and get label
                const char* main_node_shape = "box";
                node_label = parse_mermaid_node_shape(ctx, potential_node->chars, &main_node_shape);
                skip_whitespace_and_comments_mermaid(tracker);

                upsert_mermaid_node(ctx, graph, potential_node->chars, graph,
                    node_label ? node_label->chars : potential_node->chars, main_node_shape);
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
                parse_mermaid_edge_def(ctx, graph, graph, potential_node);
            } else if (!node_label) {
                // The identifier was already consumed; reparsing here used to consume
                // the following statement and lose the actual standalone node.
                ensure_mermaid_node(ctx, graph, graph, potential_node->chars);
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
