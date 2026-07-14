#include "input-graph.h"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "input-context.hpp"
#include "input-utils.hpp"
#include "source_tracker.hpp"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include <string.h>

using namespace lambda;

// Forward declarations for Mermaid parsing
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker);
static String* parse_mermaid_identifier(InputContext& ctx);
static ArrayList* parse_mermaid_edge_def(InputContext& ctx, Element* graph,
                                         Element* root_graph, const ArrayList* from_ids);
static void parse_mermaid_class_def(InputContext& ctx, Element* graph);
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id, const char** out_shape);
static bool parse_mermaid_general_shape(InputContext& ctx, const char* node_id,
                                        String** out_label, String** out_shape);
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

static bool is_direct_mermaid_edge_start(SourceTracker& tracker) {
    char current = tracker.current();
    bool endpoint_marker = (current == 'o' || current == 'x') &&
        (tracker.peek(1) == '-' || tracker.peek(1) == '=' || tracker.peek(1) == '.');
    return current == '<' || endpoint_marker || tracker.match("-->") || tracker.match("-.->") ||
        tracker.match("==>") || tracker.match("---") || tracker.match("-.-") ||
        tracker.match("===") || tracker.match("--") || current == '-' || current == '=';
}

static size_t mermaid_edge_id_length(SourceTracker& tracker) {
    char first = tracker.current();
    if (!(str_char_is_alpha(first) || first == '_')) return 0;
    size_t length = 0;
    while (str_char_is_alnum(tracker.peek(length)) || tracker.peek(length) == '_' ||
           tracker.peek(length) == '-') {
        length++;
    }
    if (tracker.peek(length) != '@') return 0;
    char edge_start = tracker.peek(length + 1);
    return edge_start == '<' || edge_start == 'o' || edge_start == 'x' ||
        edge_start == '-' || edge_start == '=' || edge_start == '.' ? length : 0;
}

static bool is_mermaid_edge_start(SourceTracker& tracker) {
    return is_direct_mermaid_edge_start(tracker) || mermaid_edge_id_length(tracker) > 0;
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

static bool parse_mermaid_class_suffix(InputContext& ctx, Element* graph,
                                        const char* node_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (!tracker.match(":::")) return false;
    tracker.advance(3);
    String* class_name = read_graph_identifier(ctx, "-", true);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name after ':::'");
        return true;
    }
    add_mermaid_metadata(ctx, graph, "class-assignment", "targets", node_id,
                         "class", class_name->chars);
    return true;
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

static const char* canonical_mermaid_shape(const char* shape) {
    if (!shape) return "box";
    if (strcmp(shape, "rect") == 0) return "box";
    if (strcmp(shape, "diam") == 0) return "diamond";
    if (strcmp(shape, "dbl-circ") == 0) return "doublecircle";
    if (strcmp(shape, "cyl") == 0) return "cylinder";
    return shape;
}

static String* parse_mermaid_property_value(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    if (tracker.current() == '"') {
        tracker.advance();
        while (!tracker.atEnd() && tracker.current() != '"' && tracker.current() != '\n') {
            if (tracker.current() == '\\' && tracker.peek(1) != '\0') tracker.advance();
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
        if (tracker.current() == '"') tracker.advance();
    } else {
        while (!tracker.atEnd() && tracker.current() != ',' && tracker.current() != '}' &&
               tracker.current() != '\n' && !str_char_is_ascii_space(tracker.current())) {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
    }
    return ctx.builder.createString(sb->str->chars, sb->str->len);
}

static bool parse_mermaid_general_shape(InputContext& ctx, const char* node_id,
                                        String** out_label, String** out_shape) {
    SourceTracker& tracker = ctx.tracker;
    if (!tracker.match("@{")) return false;
    tracker.advance(2);

    String* label = ctx.builder.createString(node_id);
    String* shape = ctx.builder.createString("box");
    while (!tracker.atEnd()) {
        skip_inline_whitespace(tracker);
        if (tracker.current() == '}') {
            tracker.advance();
            break;
        }
        String* key = read_graph_identifier(ctx, "-", true);
        if (!key) {
            ctx.addWarning(tracker.location(), "Expected property name in Mermaid node shape");
            break;
        }
        skip_inline_whitespace(tracker);
        if (tracker.current() != ':') {
            ctx.addWarning(tracker.location(), "Expected ':' after Mermaid node property");
            break;
        }
        tracker.advance();
        String* value = parse_mermaid_property_value(ctx);
        if (strcmp(key->chars, "label") == 0) {
            label = value;
        } else if (strcmp(key->chars, "shape") == 0) {
            shape = ctx.builder.createString(canonical_mermaid_shape(value->chars));
        } else {
            ctx.addWarning(tracker.location(), "Unsupported Mermaid node property '%s'", key->chars);
        }
        skip_inline_whitespace(tracker);
        if (tracker.current() == ',') tracker.advance();
    }

    *out_label = label;
    *out_shape = shape;
    return true;
}

static String* parse_mermaid_node_ref(InputContext& ctx, Element* graph,
                                      Element* root_graph) {
    String* node_id = parse_mermaid_identifier(ctx);
    if (!node_id) return nullptr;

    skip_inline_whitespace(ctx.tracker);
    char c0 = ctx.tracker.current();
    if (c0 == '[' || c0 == '(' || c0 == '{' || c0 == '>') {
        const char* shape = "box";
        String* label = parse_mermaid_node_shape(ctx, node_id->chars, &shape);
        upsert_mermaid_node(ctx, graph, node_id->chars, root_graph,
            label ? label->chars : node_id->chars, shape);
    } else if (ctx.tracker.match("@{")) {
        String* label = nullptr;
        String* shape = nullptr;
        parse_mermaid_general_shape(ctx, node_id->chars, &label, &shape);
        upsert_mermaid_node(ctx, graph, node_id->chars, root_graph,
            label ? label->chars : node_id->chars, shape ? shape->chars : "box");
    } else {
        ensure_mermaid_node(ctx, graph, root_graph, node_id->chars);
    }

    parse_mermaid_class_suffix(ctx, graph, node_id->chars);
    skip_inline_whitespace(ctx.tracker);
    return node_id;
}

static ArrayList* parse_mermaid_node_list(InputContext& ctx, Element* graph,
                                          Element* root_graph) {
    ArrayList* nodes = arraylist_new(2);
    if (!nodes) {
        ctx.addError(ctx.tracker.location(), "Unable to allocate Mermaid endpoint list");
        return nullptr;
    }

    String* node_id = parse_mermaid_node_ref(ctx, graph, root_graph);
    if (!node_id) {
        arraylist_free(nodes);
        return nullptr;
    }
    arraylist_append(nodes, node_id);

    while (ctx.tracker.current() == '&') {
        ctx.tracker.advance();
        node_id = parse_mermaid_node_ref(ctx, graph, root_graph);
        if (!node_id) {
            ctx.addError(ctx.tracker.location(), "Expected node identifier after '&'");
            arraylist_free(nodes);
            return nullptr;
        }
        arraylist_append(nodes, node_id);
    }
    return nodes;
}

static void add_mermaid_edges(InputContext& ctx, Element* graph,
                              const ArrayList* from_ids, const ArrayList* to_ids,
                              String* edge_id, String* label, const char* edge_style,
                              const char* marker_start, const char* marker_end,
                              int min_length) {
    int edge_count = from_ids->length * to_ids->length;
    int edge_ordinal = 0;
    for (int from_index = 0; from_index < from_ids->length; from_index++) {
        String* from_id = (String*)arraylist_get(from_ids, from_index);
        for (int to_index = 0; to_index < to_ids->length; to_index++) {
            String* to_id = (String*)arraylist_get(to_ids, to_index);
            Element* edge = create_edge_element(ctx.input(), from_id->chars, to_id->chars,
                label ? label->chars : nullptr, edge_style,
                strcmp(marker_start, "none") != 0 ? "true" : "false",
                strcmp(marker_end, "none") != 0 ? "true" : "false");
            if (edge_id) {
                if (edge_count == 1) {
                    add_graph_attribute(ctx.input(), edge, "id", edge_id->chars);
                } else {
                    char ordinal_text[24];
                    snprintf(ordinal_text, sizeof(ordinal_text), ":%d", edge_ordinal);
                    stringbuf_reset(ctx.sb);
                    stringbuf_append_str_n(ctx.sb, edge_id->chars, edge_id->len);
                    stringbuf_append_str(ctx.sb, ordinal_text);
                    add_graph_attribute(ctx.input(), edge, "id", ctx.sb->str->chars);
                }
            }
            add_graph_attribute(ctx.input(), edge, "arrow-tail", marker_start);
            add_graph_attribute(ctx.input(), edge, "arrow-head", marker_end);
            if (min_length > 1) {
                char length_text[16];
                snprintf(length_text, sizeof(length_text), "%d", min_length);
                add_graph_attribute(ctx.input(), edge, "min-length", length_text);
            }
            add_edge_to_graph(ctx.input(), graph, edge);
            edge_ordinal++;
        }
    }
}

// parse one Mermaid edge operator and its possibly multi-node target list.
static ArrayList* parse_mermaid_edge_def(InputContext& ctx, Element* graph,
                                         Element* root_graph, const ArrayList* from_ids) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    String* edge_id = nullptr;
    size_t edge_id_length = mermaid_edge_id_length(tracker);
    if (edge_id_length > 0) {
        edge_id = ctx.builder.createString(tracker.rest(), edge_id_length);
        tracker.advance(edge_id_length + 1);
    }

    const char* marker_start = "none";
    const char* marker_end = "none";
    String* label = nullptr;
    const char* edge_style = "solid";
    int stroke_units = 0;
    int dotted_units = 0;

    if (tracker.current() == '<') {
        marker_start = "normal";
        tracker.advance();
    } else if ((tracker.current() == 'o' || tracker.current() == 'x') &&
               (tracker.peek(1) == '-' || tracker.peek(1) == '=' || tracker.peek(1) == '.')) {
        marker_start = tracker.current() == 'o' ? "circle" : "cross";
        tracker.advance();
    }

    char first_char = tracker.current();
    if (first_char == '=') {
        edge_style = "thick";
    } else if (first_char == '.') {
        edge_style = "dotted";
        dotted_units++;
        tracker.advance();
        if (tracker.current() != '-') {
            ctx.addError(tracker.location(), "Invalid edge syntax after '.'");
            return nullptr;
        }
    } else if (first_char != '-') {
        ctx.addError(tracker.location(), "Invalid edge syntax, expected '-', '=', or '.'");
        return nullptr;
    }

    if (edge_style[0] == 't') {
        while (!tracker.atEnd() && tracker.current() == '=') {
            stroke_units++;
            tracker.advance();
        }
    } else {
        while (!tracker.atEnd() && (tracker.current() == '-' || tracker.current() == '.')) {
            if (tracker.current() == '.') {
                edge_style = "dotted";
                dotted_units++;
            } else {
                stroke_units++;
            }
            tracker.advance();
        }
    }

    if (tracker.current() == '>' || tracker.current() == 'o' || tracker.current() == 'x') {
        marker_end = tracker.current() == '>' ? "normal"
            : tracker.current() == 'o' ? "circle" : "cross";
        tracker.advance();
    }

    int min_length = dotted_units > 0
        ? dotted_units
        : stroke_units - (strcmp(marker_end, "none") != 0 ? 1 : 2);
    if (min_length < 1) min_length = 1;

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
    ArrayList* to_ids = parse_mermaid_node_list(ctx, graph, root_graph);
    if (!to_ids) {
        ctx.addError(tracker.location(), "Expected target node identifier");
        return nullptr;
    }

    add_mermaid_edges(ctx, graph, from_ids, to_ids, edge_id, label, edge_style,
                       marker_start, marker_end, min_length);
    return to_ids;
}

static void parse_mermaid_edge_chain(InputContext& ctx, Element* graph, Element* root_graph,
                                     ArrayList* from_ids) {
    ArrayList* edge_from = from_ids;
    while (is_mermaid_edge_start(ctx.tracker)) {
        ArrayList* edge_to = parse_mermaid_edge_def(ctx, graph, root_graph, edge_from);
        arraylist_free(edge_from);
        if (!edge_to) return;
        // the previous target set is the source set for the next chained operator.
        edge_from = edge_to;
        skip_inline_whitespace(ctx.tracker);
    }
    arraylist_free(edge_from);
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

        ArrayList* potential_nodes = parse_mermaid_node_list(ctx, subgraph_elem, root_graph);
        if (potential_nodes) {
            if (is_mermaid_edge_start(tracker)) {
                parse_mermaid_edge_chain(ctx, subgraph_elem, root_graph, potential_nodes);
            } else {
                arraylist_free(potential_nodes);
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

        ArrayList* potential_nodes = parse_mermaid_node_list(ctx, graph, graph);
        if (potential_nodes) {
            if (is_mermaid_edge_start(tracker)) {
                parse_mermaid_edge_chain(ctx, graph, graph, potential_nodes);
            } else {
                arraylist_free(potential_nodes);
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
