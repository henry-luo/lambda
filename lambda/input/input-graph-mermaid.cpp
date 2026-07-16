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
                                         Element* root_graph, const ArrayList* from_ids,
                                         ArrayList* statement_edges, int chain_segment);
static void parse_mermaid_class_def(InputContext& ctx, Element* graph,
                                    const SourceLocation& source_start,
                                    bool force_nodes = false);
static String* parse_mermaid_interaction_token(InputContext& ctx);
static void parse_mermaid_interaction(InputContext& ctx, Element* graph,
                                      const SourceLocation& source_start,
                                      const char* forced_action = nullptr);
static void parse_mermaid_edge_properties(InputContext& ctx, Element* graph,
                                          Element* root_graph,
                                          const SourceLocation& source_start);
static void parse_mermaid_style_assignment(InputContext& ctx, Element* graph,
                                           const SourceLocation& source_start,
                                           bool edge_assignment);
static void parse_mermaid_accessibility(InputContext& ctx, Element* graph,
                                        const SourceLocation& source_start,
                                        const char* value_tag, bool allow_block);
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id,
                                         const char** out_shape, const char** out_label_format);
static bool parse_mermaid_general_shape(InputContext& ctx, const char* node_id,
                                        const char* default_label, const char* default_shape,
                                        String** out_label, String** out_shape,
                                        const char** out_label_format);
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph, Element* root_graph,
                                   const SourceLocation& source_start);
static void parse_mermaid_subgraph_content(InputContext& ctx, Element* subgraph_elem,
                                            Element* root_graph);
static void parse_mermaid_class_diagram(InputContext& ctx, Element* graph);
static void parse_mermaid_er_diagram(InputContext& ctx, Element* graph);
static void parse_mermaid_state_diagram(InputContext& ctx, Element* graph);

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

static void parse_mermaid_family_direction(InputContext& ctx, Element* graph,
                                            const SourceLocation& source_start,
                                            const char* code, const char* family) {
    char direction[3] = {0};
    if (parse_mermaid_direction(ctx.tracker, direction)) {
        add_graph_attribute(ctx.input(), graph, "direction", normalize_direction(direction));
        add_graph_attribute(ctx.input(), graph, "rank-dir", normalize_direction(direction));
    } else {
        ctx.addErrorCode(source_start, code,
            "Expected %s diagram direction LR, RL, TB, TD, or BT", family);
    }
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

static size_t mermaid_property_target_length(SourceTracker& tracker) {
    char first = tracker.current();
    if (!(str_char_is_alpha(first) || first == '_')) return 0;
    size_t length = 0;
    while (str_char_is_alnum(tracker.peek(length)) || tracker.peek(length) == '_' ||
           tracker.peek(length) == '-') {
        length++;
    }
    return tracker.peek(length) == '@' && tracker.peek(length + 1) == '{' ? length : 0;
}

static bool is_mermaid_edge_start(SourceTracker& tracker) {
    return is_direct_mermaid_edge_start(tracker) || mermaid_edge_id_length(tracker) > 0;
}

static Element* find_direct_mermaid_child(Element* parent, const char* tag,
                                           const char* id = nullptr) {
    if (!parent || !tag) return nullptr;
    ElementReader graph_reader(parent);
    ElementReader child;
    auto children = graph_reader.childElements();
    while (children.next(&child)) {
        if (!child.hasTag(tag)) continue;
        if (!id) return (Element*)child.element();
        const char* child_id = child.get_attr_string("id");
        if (child_id && strcmp(child_id, id) == 0) {
            return (Element*)child.element();
        }
    }
    return nullptr;
}

static Element* find_direct_node(Element* graph, const char* id) {
    return find_direct_mermaid_child(graph, "node", id);
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

static Element* find_mermaid_edge(Element* graph, const char* id) {
    ElementReader graph_reader(graph);
    ElementReader child;
    auto children = graph_reader.childElements();
    while (children.next(&child)) {
        if (child.hasTag("edge")) {
            const char* edge_id = child.get_attr_string("id");
            const char* source_id = child.get_attr_string("source-id");
            if ((edge_id && strcmp(edge_id, id) == 0) ||
                (source_id && strcmp(source_id, id) == 0)) {
                return (Element*)child.element();
            }
        } else if (child.hasTag("subgraph")) {
            Element* edge = find_mermaid_edge((Element*)child.element(), id);
            if (edge) return edge;
        }
    }
    return nullptr;
}

static Element* ensure_mermaid_node(InputContext& ctx, Element* graph, Element* root_graph,
                                     const char* id) {
    Element* node = find_mermaid_node(graph, id);
    if (!node && graph != root_graph) node = find_mermaid_node(root_graph, id);
    if (node) return node;

    // mermaid edges declare endpoints implicitly, so source Mark must materialize
    // both identities before canonical endpoint resolution.
    node = create_node_element(ctx.input(), id, id, "box");
    add_node_to_graph(ctx.input(), graph, node);
    return node;
}

static Element* add_mermaid_node_declaration(InputContext& ctx, Element* graph,
                                             const char* id, const char* label,
                                             const char* shape,
                                             const char* label_format) {
    // explicit declarations retain their own spans and values until the Lambda
    // normalizer applies Mermaid's graph-global redeclaration semantics.
    Element* node = create_node_element(ctx.input(), id, label, shape);
    if (label_format) add_graph_attribute(ctx.input(), node, "label-format", label_format);
    add_node_to_graph(ctx.input(), graph, node);
    return node;
}

static String* create_trimmed_mermaid_text(InputContext& ctx, const char* start,
                                           size_t length, bool strip_semicolon) {
    while (length > 0 && str_char_is_ascii_space(*start)) {
        start++;
        length--;
    }
    while (length > 0 && (str_char_is_ascii_space(start[length - 1]) ||
           (strip_semicolon && start[length - 1] == ';'))) {
        length--;
    }
    return ctx.builder.createString(start, length);
}

static String* parse_mermaid_line_text(InputContext& ctx, bool strip_semicolon) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* start = tracker.rest();
    while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
    return create_trimmed_mermaid_text(ctx, start,
        (size_t)(tracker.rest() - start), strip_semicolon);
}

static String* parse_mermaid_front_matter(InputContext& ctx,
                                           SourceLocation* source_end) {
    SourceTracker& tracker = ctx.tracker;
    if (!tracker.match("---") ||
        (tracker.peek(3) != '\n' && tracker.peek(3) != '\r')) return nullptr;
    tracker.advance(3);
    while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
    if (tracker.current() == '\n') tracker.advance();
    const char* value_start = tracker.rest();

    while (!tracker.atEnd()) {
        const char* line_start = tracker.rest();
        if (tracker.match("---") &&
            (tracker.peek(3) == '\0' || tracker.peek(3) == '\n' || tracker.peek(3) == '\r')) {
            String* value = create_trimmed_mermaid_text(ctx, value_start,
                (size_t)(line_start - value_start), false);
            tracker.advance(3);
            while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
            if (tracker.current() == '\n') tracker.advance();
            *source_end = tracker.location();
            return value;
        }
        while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
        if (tracker.current() == '\n') tracker.advance();
    }
    ctx.addWarning(tracker.location(), "Unterminated Mermaid front matter");
    *source_end = tracker.location();
    return create_trimmed_mermaid_text(ctx, value_start,
        (size_t)(tracker.rest() - value_start), false);
}

static String* parse_mermaid_init_directive(InputContext& ctx,
                                             SourceLocation* source_end) {
    SourceTracker& tracker = ctx.tracker;
    if (!tracker.match("%%{")) return nullptr;
    tracker.advance(3);
    const char* value_start = tracker.rest();
    while (!tracker.atEnd() && !tracker.match("}%%")) tracker.advance();
    String* value = create_trimmed_mermaid_text(ctx, value_start,
        (size_t)(tracker.rest() - value_start), false);
    if (tracker.match("}%%")) tracker.advance(3);
    else ctx.addWarning(tracker.location(), "Unterminated Mermaid initialization directive");
    *source_end = tracker.location();
    return value;
}

static bool mermaid_label_has_html_tag(const char* text, size_t length) {
    for (size_t index = 0; index + 2 < length; index++) {
        if (text[index] != '<') continue;
        size_t name_index = index + 1;
        if (text[name_index] == '/') name_index++;
        if (name_index < length && str_char_is_alpha(text[name_index])) {
            for (size_t close_index = name_index + 1; close_index < length; close_index++) {
                if (text[close_index] == '>') return true;
                if (text[close_index] == '<' || text[close_index] == '\n') break;
            }
        }
    }
    return false;
}

static String* normalize_mermaid_label(InputContext& ctx, const char* text, size_t length,
                                        const char** out_format) {
    if (length >= 2 && text[0] == '"' && text[length - 1] == '"') {
        text++;
        length -= 2;
    }
    if (length >= 2 && text[0] == '`' && text[length - 1] == '`') {
        // Mermaid's outer backticks select Markdown; they are format delimiters,
        // while Markdown markers inside the label remain source-level content.
        *out_format = "markdown";
        return ctx.builder.createString(text + 1, length - 2);
    }
    *out_format = mermaid_label_has_html_tag(text, length) ? "html" : "text";
    return ctx.builder.createString(text, length);
}

static void add_mermaid_meta_value(InputContext& ctx, Element* graph, const char* value_tag,
                                    String* value, const SourceLocation& source_start,
                                    const SourceLocation& source_end) {
    Element* meta = find_direct_mermaid_child(graph, "meta");
    if (!meta) {
        meta = ctx.builder.element("meta").final().element;
        add_node_to_graph(ctx.input(), graph, meta);
    }
    Element* semantic_value = ctx.builder.element(value_tag)
        .attr("value", value ? value->chars : "")
        .final().element;
    graph_set_source_span(ctx, semantic_value, source_start, source_end, false);
    add_node_to_graph(ctx.input(), meta, semantic_value);
}

static bool parse_mermaid_class_suffix(InputContext& ctx, Element* graph,
                                        const char* node_id) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    SourceLocation source_start = tracker.location();
    if (tracker.match(":::")) tracker.advance(3);
    else if (tracker.current() == '.') tracker.advance();
    else return false;
    String* class_name = read_graph_identifier(ctx, "-", true);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name after ':::'");
        return true;
    }
    ElementBuilder assignment = ctx.builder.element("class-assignment");
    assignment.attr("target-kind", "node");
    assignment.attr("targets", node_id);
    assignment.attr("class", class_name->chars);
    Element* result = assignment.final().element;
    graph_set_source_span(ctx, result, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, result);
    return true;
}

// skip whitespace and %% line comments
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker) {
    bool advanced = true;
    while (advanced) {
        advanced = false;
        while (!tracker.atEnd() && str_char_is_ascii_space(tracker.current())) {
            tracker.advance();
            advanced = true;
        }
        if (tracker.current() == ';') {
            tracker.advance();
            advanced = true;
        } else if (tracker.match("%%") && !tracker.match("%%{")) {
            while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
            advanced = true;
        }
    }
}

// read Mermaid identifier: [A-Za-z_][A-Za-z0-9_\-]* (whitespace skipped first)
static String* parse_mermaid_identifier(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (tracker.atEnd()) return nullptr;
    if (tracker.current() == '`') {
        tracker.advance();
        const char* start = tracker.rest();
        while (!tracker.atEnd() && tracker.current() != '`' && tracker.current() != '\n') {
            tracker.advance();
        }
        String* escaped = ctx.builder.createString(start,
            (size_t)(tracker.rest() - start));
        if (tracker.current() == '`') tracker.advance();
        return escaped;
    }
    unsigned char first = (unsigned char)tracker.current();
    if (!(str_char_is_alnum(tracker.current()) || tracker.current() == '_' || first >= 0x80)) {
        return nullptr;
    }
    const char* start = tracker.rest();
    size_t length = 0;
    while (!tracker.atEnd()) {
        char current = tracker.current();
        unsigned char byte = (unsigned char)current;
        if (current == '-' && (tracker.peek(1) == '-' || tracker.peek(1) == '.' ||
                               tracker.peek(1) == '=' || tracker.peek(1) == '>')) {
            break;
        }
        if (!(str_char_is_alnum(current) || current == '_' || current == '-' || byte >= 0x80)) {
            break;
        }
        tracker.advance();
        length++;
    }
    return length > 0 ? ctx.builder.createString(start, length) : nullptr;
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
static String* parse_mermaid_node_shape(InputContext& ctx, const char* node_id,
                                         const char** out_shape, const char** out_label_format) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    if (tracker.atEnd()) {
        *out_shape = "box";
        *out_label_format = "text";
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
        *out_label_format = "text";
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

    return normalize_mermaid_label(ctx, sb->str->chars, sb->str->len, out_label_format);
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
                                        const char* default_label, const char* default_shape,
                                        String** out_label, String** out_shape,
                                        const char** out_label_format) {
    SourceTracker& tracker = ctx.tracker;
    if (!tracker.match("@{")) return false;
    tracker.advance(2);

    String* label = ctx.builder.createString(default_label ? default_label : node_id);
    String* shape = ctx.builder.createString(default_shape ? default_shape : "box");
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

    *out_label = normalize_mermaid_label(ctx, label->chars, label->len, out_label_format);
    *out_shape = shape;
    return true;
}

static String* parse_mermaid_node_ref(InputContext& ctx, Element* graph,
                                      Element* root_graph) {
    skip_inline_whitespace(ctx.tracker);
    SourceLocation source_start = ctx.tracker.location();
    String* node_id = parse_mermaid_identifier(ctx);
    if (!node_id) return nullptr;

    skip_inline_whitespace(ctx.tracker);
    char c0 = ctx.tracker.current();
    Element* node = nullptr;
    if (c0 == '[' || c0 == '(' || c0 == '{' || c0 == '>') {
        const char* shape = "box";
        const char* label_format = "text";
        String* label = parse_mermaid_node_shape(ctx, node_id->chars, &shape, &label_format);
        if (ctx.tracker.match("@{")) {
            String* property_label = nullptr;
            String* property_shape = nullptr;
            parse_mermaid_general_shape(ctx, node_id->chars,
                label ? label->chars : node_id->chars, shape,
                &property_label, &property_shape, &label_format);
            label = property_label;
            shape = property_shape ? property_shape->chars : shape;
        }
        node = add_mermaid_node_declaration(ctx, graph, node_id->chars,
            label ? label->chars : node_id->chars, shape, label_format);
    } else if (ctx.tracker.match("@{")) {
        String* label = nullptr;
        String* shape = nullptr;
        const char* label_format = "text";
        parse_mermaid_general_shape(ctx, node_id->chars, node_id->chars, "box",
                                    &label, &shape, &label_format);
        node = add_mermaid_node_declaration(ctx, graph, node_id->chars,
            label ? label->chars : node_id->chars, shape ? shape->chars : "box", label_format);
    } else {
        node = ensure_mermaid_node(ctx, graph, root_graph, node_id->chars);
    }

    parse_mermaid_class_suffix(ctx, graph, node_id->chars);
    graph_set_source_span(ctx, node, source_start, ctx.tracker.location(), true);
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
                              String* edge_id, String* label, const char* label_format,
                              const char* edge_style,
                              const char* marker_start, const char* marker_end,
                              int min_length, const SourceLocation& source_start,
                              const SourceLocation& source_end,
                              ArrayList* statement_edges, int chain_segment) {
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
            if (label && label_format) {
                add_graph_attribute(ctx.input(), edge, "label-format", label_format);
            }
            if (edge_id) {
                add_graph_attribute(ctx.input(), edge, "source-id", edge_id->chars);
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
            add_graph_integer_attribute(ctx.input(), edge, "source-segment-index",
                                        (int64_t)chain_segment);
            graph_set_source_span(ctx, edge, source_start, source_end, false);
            add_edge_to_graph(ctx.input(), graph, edge);
            arraylist_append(statement_edges, edge);
            edge_ordinal++;
        }
    }
}

static const char* find_mermaid_inline_edge_close(const char* start,
                                                   const char* pattern) {
    size_t pattern_length = strlen(pattern);
    for (const char* cursor = start; *cursor && *cursor != '\n'; cursor++) {
        if (strncmp(cursor, pattern, pattern_length) == 0) return cursor;
    }
    return nullptr;
}

static void set_mermaid_edge_statement_provenance(InputContext& ctx,
                                                   ArrayList* statement_edges,
                                                   const SourceLocation& source_start,
                                                   const SourceLocation& source_end) {
    int64_t expansion_count = (int64_t)statement_edges->length;
    for (int index = 0; index < statement_edges->length; index++) {
        Element* edge = (Element*)arraylist_get(statement_edges, index);
        // expanded edge products retain both their operator span and the common
        // authored statement, so downstream merging never has to infer chains.
        add_graph_integer_attribute(ctx.input(), edge, "source-statement-start",
                                    (int64_t)source_start.offset);
        add_graph_integer_attribute(ctx.input(), edge, "source-statement-end",
                                    (int64_t)source_end.offset);
        add_graph_integer_attribute(ctx.input(), edge, "source-statement-line",
                                    (int64_t)source_start.line);
        add_graph_integer_attribute(ctx.input(), edge, "source-statement-column",
                                    (int64_t)source_start.column);
        add_graph_integer_attribute(ctx.input(), edge, "source-expansion-index",
                                    (int64_t)index);
        add_graph_integer_attribute(ctx.input(), edge, "source-expansion-count",
                                    expansion_count);
    }
}

// parse one Mermaid edge operator and its possibly multi-node target list.
static ArrayList* parse_mermaid_edge_def(InputContext& ctx, Element* graph,
                                         Element* root_graph, const ArrayList* from_ids,
                                         ArrayList* statement_edges, int chain_segment) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);
    SourceLocation source_start = tracker.location();

    String* edge_id = nullptr;
    size_t edge_id_length = mermaid_edge_id_length(tracker);
    if (edge_id_length > 0) {
        edge_id = ctx.builder.createString(tracker.rest(), edge_id_length);
        tracker.advance(edge_id_length + 1);
    }

    const char* marker_start = "none";
    const char* marker_end = "none";
    String* label = nullptr;
    const char* label_format = nullptr;
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


    if (strcmp(marker_end, "none") == 0) {
        skip_inline_whitespace(tracker);
        const char* label_start = tracker.rest();
        const char* close_pattern = strcmp(edge_style, "thick") == 0 ? "==>"
            : strcmp(edge_style, "dotted") == 0 ? ".->" : "-->";
        const char* close = find_mermaid_inline_edge_close(label_start, close_pattern);
        if (close && close > label_start) {
            String* raw_label = create_trimmed_mermaid_text(ctx, label_start,
                (size_t)(close - label_start), false);
            label = normalize_mermaid_label(ctx, raw_label->chars, raw_label->len,
                                             &label_format);
            tracker.advance((size_t)(close - label_start) + strlen(close_pattern));
            marker_end = "normal";
        }
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
            label = normalize_mermaid_label(ctx, sb->str->chars, sb->str->len,
                                             &label_format);
        }
    }

    skip_whitespace_and_comments_mermaid(tracker);
    ArrayList* to_ids = parse_mermaid_node_list(ctx, graph, root_graph);
    if (!to_ids) {
        ctx.addError(tracker.location(), "Expected target node identifier");
        return nullptr;
    }

    add_mermaid_edges(ctx, graph, from_ids, to_ids, edge_id, label, label_format, edge_style,
                       marker_start, marker_end, min_length, source_start,
                       tracker.location(), statement_edges, chain_segment);
    return to_ids;
}

static void parse_mermaid_edge_chain(InputContext& ctx, Element* graph, Element* root_graph,
                                     ArrayList* from_ids,
                                     const SourceLocation& statement_start) {
    ArrayList* statement_edges = arraylist_new(4);
    if (!statement_edges) {
        ctx.addError(statement_start, "Unable to allocate Mermaid edge provenance list");
        arraylist_free(from_ids);
        return;
    }
    ArrayList* edge_from = from_ids;
    int chain_segment = 0;
    while (is_mermaid_edge_start(ctx.tracker)) {
        ArrayList* edge_to = parse_mermaid_edge_def(ctx, graph, root_graph, edge_from,
                                                    statement_edges, chain_segment);
        arraylist_free(edge_from);
        if (!edge_to) {
            arraylist_free(statement_edges);
            return;
        }
        // the previous target set is the source set for the next chained operator.
        edge_from = edge_to;
        chain_segment++;
        skip_inline_whitespace(ctx.tracker);
    }
    set_mermaid_edge_statement_provenance(ctx, statement_edges, statement_start,
                                          ctx.tracker.location());
    arraylist_free(edge_from);
    arraylist_free(statement_edges);
}

// Parse Mermaid class assignment (for styling): class nodeIds className
static void parse_mermaid_class_def(InputContext& ctx, Element* graph,
                                    const SourceLocation& source_start,
                                    bool force_nodes) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    StringBuf* targets = ctx.sb;
    stringbuf_reset(targets);
    bool all_edges = true;
    bool quoted = false;

    if (tracker.current() == '"' || tracker.current() == '\'') {
        String* quoted_targets = parse_mermaid_interaction_token(ctx);
        if (quoted_targets) {
            // The token parser already populated ctx.sb; appending its retained value
            // back into the same buffer duplicates every quoted cssClass target list.
            all_edges = false;
            quoted = true;
        }
    }

    // parse node IDs (can be comma-separated)
    while (!quoted && !tracker.atEnd() &&
           !str_char_is_ascii_space(tracker.current())) {
        String* node_id = parse_mermaid_identifier(ctx);
        if (!node_id) break;

        if (!find_mermaid_edge(graph, node_id->chars)) all_edges = false;

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

    Element* assignment = ctx.builder.element("class-assignment")
        .attr("target-kind", !force_nodes && all_edges ? "edge" : "node")
        .attr("targets", targets->str->chars)
        .attr("class", class_name->chars)
        .final().element;
    graph_set_source_span(ctx, assignment, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, assignment);
}

// Parse Mermaid classDef into semantic style metadata without interpreting CSS here.
static void parse_mermaid_style_rule(InputContext& ctx, Element* graph,
                                      const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    ArrayList* class_names = arraylist_new(2);
    if (!class_names) {
        ctx.addError(source_start, "Unable to allocate Mermaid class definition list");
        skip_to_eol(tracker);
        return;
    }
    while (!tracker.atEnd()) {
        String* class_name = parse_mermaid_identifier(ctx);
        if (!class_name) break;
        arraylist_append(class_names, class_name);
        skip_inline_whitespace(tracker);
        if (tracker.current() != ',') break;
        tracker.advance();
        skip_inline_whitespace(tracker);
    }
    if (!class_names || class_names->length == 0) {
        ctx.addWarning(tracker.location(), "Expected class name after classDef");
        skip_to_eol(tracker);
        if (class_names) arraylist_free(class_names);
        return;
    }

    String* declarations = parse_mermaid_line_text(ctx, true);
    for (int index = 0; index < class_names->length; index++) {
        String* class_name = (String*)arraylist_get(class_names, index);
        Element* rule = ctx.builder.element("style-rule")
            .attr("class", class_name->chars)
            .attr("declarations", declarations->chars)
            .final().element;
        graph_set_source_span(ctx, rule, source_start, tracker.location(), false);
        add_node_to_graph(ctx.input(), graph, rule);
    }
    arraylist_free(class_names);
}

static void parse_mermaid_style_assignment(InputContext& ctx, Element* graph,
                                           const SourceLocation& source_start,
                                           bool edge_assignment) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* line_start = tracker.rest();
    while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
    const char* line_end = tracker.rest();

    const char* first_colon = line_start;
    while (first_colon < line_end && *first_colon != ':') first_colon++;
    if (first_colon == line_end) {
        ctx.addWarning(source_start, "Expected CSS declarations in Mermaid %s statement",
            edge_assignment ? "linkStyle" : "style");
        return;
    }
    const char* declarations_start = first_colon;
    while (declarations_start > line_start &&
           !str_char_is_ascii_space(declarations_start[-1])) {
        declarations_start--;
    }

    stringbuf_reset(ctx.sb);
    for (const char* cursor = line_start; cursor < declarations_start; cursor++) {
        if (!str_char_is_ascii_space(*cursor)) stringbuf_append_char(ctx.sb, *cursor);
    }
    String* targets = ctx.builder.createString(ctx.sb->str->chars, ctx.sb->str->len);
    String* declarations = create_trimmed_mermaid_text(ctx, declarations_start,
        (size_t)(line_end - declarations_start), true);
    if (!targets || targets->len == 0 || !declarations || declarations->len == 0) {
        ctx.addWarning(source_start, "Expected targets and declarations in Mermaid %s statement",
            edge_assignment ? "linkStyle" : "style");
        return;
    }

    Element* assignment = ctx.builder.element("style-assignment")
        .attr("target-kind", edge_assignment ? "edge" : "node")
        .attr("targets", targets->chars)
        .attr("declarations", declarations->chars)
        .final().element;
    graph_set_source_span(ctx, assignment, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, assignment);
}

static String* parse_mermaid_interaction_token(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (tracker.atEnd() || tracker.current() == '\n' || tracker.current() == ';') return nullptr;

    StringBuf* token = ctx.sb;
    stringbuf_reset(token);
    char quote = 0;
    int parentheses = 0;
    if (tracker.current() == '"' || tracker.current() == '\'') {
        quote = tracker.current();
        tracker.advance();
    }
    while (!tracker.atEnd()) {
        char current = tracker.current();
        if ((quote && current == quote) ||
            (!quote && parentheses == 0 &&
             (str_char_is_ascii_space(current) || current == ';'))) {
            break;
        }
        if (!quote && current == '(') parentheses++;
        else if (!quote && current == ')' && parentheses > 0) parentheses--;
        if (current == '\\' && tracker.peek(1) != '\0') tracker.advance();
        stringbuf_append_char(token, tracker.current());
        tracker.advance();
    }
    if (quote && tracker.current() == quote) tracker.advance();
    return ctx.builder.createString(token->str->chars, token->str->len);
}

static bool is_mermaid_link_target(String* value) {
    if (!value) return false;
    return strcmp(value->chars, "_blank") == 0 || strcmp(value->chars, "_self") == 0 ||
           strcmp(value->chars, "_parent") == 0 || strcmp(value->chars, "_top") == 0;
}

static void parse_mermaid_interaction(InputContext& ctx, Element* graph,
                                      const SourceLocation& source_start,
                                      const char* forced_action) {
    SourceTracker& tracker = ctx.tracker;
    String* target = parse_mermaid_interaction_token(ctx);
    if (!target || target->len == 0) {
        ctx.addWarning(source_start, "Expected target after Mermaid click directive");
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    const char* action = forced_action ? forced_action : "callback";
    if (!forced_action && consume_keyword(tracker, "href")) action = "link";
    else if (!forced_action && consume_keyword(tracker, "call")) action = "callback";
    else if (!forced_action &&
             (tracker.current() == '"' || tracker.current() == '\'')) action = "link";

    String* value = parse_mermaid_interaction_token(ctx);
    String* tooltip = parse_mermaid_interaction_token(ctx);
    String* target_window = parse_mermaid_interaction_token(ctx);
    if (!target_window && is_mermaid_link_target(tooltip)) {
        target_window = tooltip;
        tooltip = nullptr;
    }
    if (!value || value->len == 0) {
        ctx.addWarning(source_start, "Expected link or callback after Mermaid click target");
        return;
    }

    ElementBuilder interaction = ctx.builder.element("interaction");
    interaction.attr("target", target->chars);
    interaction.attr("action", action);
    interaction.attr(strcmp(action, "link") == 0 ? "href" : "callback", value->chars);
    if (tooltip && tooltip->len > 0) interaction.attr("tooltip", tooltip->chars);
    if (target_window && target_window->len > 0) {
        interaction.attr("target-window", target_window->chars);
    }
    Element* result = interaction.final().element;
    graph_set_source_span(ctx, result, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, result);
}

static void parse_mermaid_edge_properties(InputContext& ctx, Element* graph,
                                          Element* root_graph,
                                          const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    size_t target_length = mermaid_property_target_length(tracker);
    if (target_length == 0) return;
    String* target = ctx.builder.createString(tracker.rest(), target_length);
    tracker.advance(target_length + 2);
    if (!find_mermaid_edge(root_graph, target->chars)) {
        ctx.addWarning(source_start, "Mermaid edge property target '%s' is not declared",
                       target->chars);
    }

    while (!tracker.atEnd()) {
        skip_inline_whitespace(tracker);
        if (tracker.current() == '}') {
            tracker.advance();
            break;
        }
        String* key = read_graph_identifier(ctx, "-", true);
        if (!key) {
            ctx.addWarning(tracker.location(), "Expected Mermaid edge property name");
            break;
        }
        skip_inline_whitespace(tracker);
        if (tracker.current() != ':') {
            ctx.addWarning(tracker.location(), "Expected ':' after Mermaid edge property");
            break;
        }
        tracker.advance();
        String* value = parse_mermaid_property_value(ctx);
        Element* property = ctx.builder.element("edge-property")
            .attr("target", target->chars)
            .attr("key", key->chars)
            .attr("value", value ? value->chars : "")
            .final().element;
        graph_set_source_span(ctx, property, source_start, tracker.location(), false);
        add_node_to_graph(ctx.input(), graph, property);
        skip_inline_whitespace(tracker);
        if (tracker.current() == ',') tracker.advance();
    }
}

static void parse_mermaid_accessibility(InputContext& ctx, Element* graph,
                                        const SourceLocation& source_start,
                                        const char* value_tag, bool allow_block) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (tracker.current() == ':') {
        tracker.advance();
        String* value = parse_mermaid_line_text(ctx, false);
        add_mermaid_meta_value(ctx, graph, value_tag, value, source_start, tracker.location());
        return;
    }

    if (allow_block && tracker.current() == '{') {
        tracker.advance();
        const char* value_start = tracker.rest();
        while (!tracker.atEnd() && tracker.current() != '}') tracker.advance();
        String* value = create_trimmed_mermaid_text(ctx, value_start,
            (size_t)(tracker.rest() - value_start), false);
        if (tracker.current() == '}') tracker.advance();
        else ctx.addWarning(source_start, "Unterminated Mermaid accessibility description");
        add_mermaid_meta_value(ctx, graph, value_tag, value, source_start, tracker.location());
        return;
    }

    ctx.addWarning(source_start, "Expected ':'%s after Mermaid accessibility directive",
        allow_block ? " or '{...}'" : "");
    skip_to_eol(tracker);
}

// Parse Mermaid subgraph: subgraph id [label] ... end
// Supports direction override: direction LR/TB/BT/RL
static void parse_mermaid_subgraph(InputContext& ctx, Element* parent_graph, Element* root_graph,
                                   const SourceLocation& source_start) {
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
    graph_set_source_span(ctx, subgraph_elem, source_start, tracker.location(), false);

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

        SourceLocation statement_start = tracker.location();

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
            parse_mermaid_subgraph(ctx, subgraph_elem, root_graph, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, subgraph_elem, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, subgraph_elem, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "click")) {
            parse_mermaid_interaction(ctx, subgraph_elem, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, subgraph_elem, statement_start, false);
            continue;
        }

        if (consume_keyword(tracker, "linkStyle")) {
            parse_mermaid_style_assignment(ctx, subgraph_elem, statement_start, true);
            continue;
        }

        if (mermaid_property_target_length(tracker) > 0) {
            size_t target_length = mermaid_property_target_length(tracker);
            String* target = ctx.builder.createString(tracker.rest(), target_length);
            if (find_mermaid_edge(root_graph, target->chars)) {
                parse_mermaid_edge_properties(ctx, subgraph_elem, root_graph, statement_start);
                continue;
            }
        }

        const char* line_start = tracker.rest();

        ArrayList* potential_nodes = parse_mermaid_node_list(ctx, subgraph_elem, root_graph);
        if (potential_nodes) {
            if (is_mermaid_edge_start(tracker)) {
                parse_mermaid_edge_chain(ctx, subgraph_elem, root_graph, potential_nodes,
                                          statement_start);
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

struct MermaidClassRef {
    String* id;
    String* generic;
    String* display;
};

struct MermaidClassRelation {
    String* token;
    const char* style;
    const char* marker_start;
    const char* marker_end;
};

static String* mermaid_class_display(InputContext& ctx, const char* value, size_t length) {
    StringBuf* display = ctx.sb;
    stringbuf_reset(display);
    for (size_t index = 0; index < length; index++) {
        if (value[index] != '~' || index == 0) {
            stringbuf_append_char(display, value[index]);
            continue;
        }
        char next = index + 1 < length ? value[index + 1] : '\0';
        bool opens = next != '~' && next != '\0' && next != ' ' && next != '\t' &&
            next != '\r' && next != '\n' && next != ';' && next != ',' && next != ')';
        stringbuf_append_char(display, opens ? '<' : '>');
    }
    return ctx.builder.createString(display->str->chars, display->str->len);
}

static MermaidClassRef parse_mermaid_class_ref(InputContext& ctx) {
    MermaidClassRef ref = {};
    ref.id = parse_mermaid_identifier(ctx);
    if (!ref.id) return ref;
    ref.display = ref.id;
    SourceTracker& tracker = ctx.tracker;
    if (tracker.current() != '~') return ref;

    const char* generic_start = tracker.rest();
    while (!tracker.atEnd() && !str_char_is_ascii_space(tracker.current()) &&
           tracker.current() != '[' && tracker.current() != '{' && tracker.current() != ':' &&
           tracker.current() != ';' && tracker.current() != ',') {
        tracker.advance();
    }
    ref.generic = ctx.builder.createString(generic_start,
        (size_t)(tracker.rest() - generic_start));
    StringBuf* source = ctx.sb;
    stringbuf_reset(source);
    stringbuf_append_str_n(source, ref.id->chars, ref.id->len);
    stringbuf_append_str_n(source, ref.generic->chars, ref.generic->len);
    // The display helper reuses ctx.sb, so retain the source before it resets that buffer.
    String* source_text = ctx.builder.createString(source->str->chars, source->str->len);
    ref.display = mermaid_class_display(ctx, source_text->chars, source_text->len);
    return ref;
}

static Element* ensure_mermaid_class_node(InputContext& ctx, Element* graph,
                                          Element* root_graph,
                                          const MermaidClassRef& ref) {
    Element* node = ensure_mermaid_node(ctx, graph, root_graph, ref.id->chars);
    add_graph_attribute(ctx.input(), node, "mermaid-family", "class");
    if (ref.generic) {
        add_graph_attribute(ctx.input(), node, "generic", ref.generic->chars);
        add_graph_attribute(ctx.input(), node, "label", ref.display->chars);
    }
    return node;
}

static const char* consume_mermaid_class_marker(SourceTracker& tracker, bool start) {
    if (start && tracker.match("<|")) {
        tracker.advance(2);
        return "empty";
    }
    if (!start && tracker.match("|>")) {
        tracker.advance(2);
        return "empty";
    }
    if (tracker.match("()")) {
        tracker.advance(2);
        return "circle";
    }
    if (tracker.current() == '*') {
        tracker.advance();
        return "diamond";
    }
    if (tracker.current() == 'o') {
        tracker.advance();
        return "odiamond";
    }
    if ((start && tracker.current() == '<') || (!start && tracker.current() == '>')) {
        tracker.advance();
        return "normal";
    }
    return "none";
}

static bool consume_mermaid_class_relation(InputContext& ctx,
                                            MermaidClassRelation* relation) {
    SourceTracker& tracker = ctx.tracker;
    const char* token_start = tracker.rest();
    relation->marker_start = consume_mermaid_class_marker(tracker, true);
    if (tracker.match("--")) {
        relation->style = "solid";
        tracker.advance(2);
    } else if (tracker.match("..")) {
        relation->style = "dotted";
        tracker.advance(2);
    } else {
        return false;
    }
    relation->marker_end = consume_mermaid_class_marker(tracker, false);
    relation->token = ctx.builder.createString(token_start,
        (size_t)(tracker.rest() - token_start));
    return true;
}

static String* parse_mermaid_class_annotation(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    if (!tracker.match("<<")) return nullptr;
    const char* start = tracker.rest();
    tracker.advance(2);
    while (!tracker.atEnd() && !tracker.match(">>") && tracker.current() != '\n') {
        tracker.advance();
    }
    if (tracker.match(">>")) tracker.advance(2);
    return ctx.builder.createString(start, (size_t)(tracker.rest() - start));
}

static void add_mermaid_class_annotation(InputContext& ctx, Element* node, String* value,
                                         const SourceLocation& source_start) {
    if (!node || !value) return;
    Element* member = ctx.builder.element("class-member")
        .attr("kind", "stereotype").attr("value", value->chars)
        .attr("display", value->chars).final().element;
    graph_set_source_span(ctx, member, source_start, ctx.tracker.location(), false);
    add_node_to_graph(ctx.input(), node, member);
}

static String* parse_mermaid_class_note_text(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (tracker.current() != '"' && tracker.current() != '\'') {
        return parse_mermaid_line_text(ctx, true);
    }
    char quote = tracker.current();
    tracker.advance();
    StringBuf* text = ctx.sb;
    stringbuf_reset(text);
    while (!tracker.atEnd() && tracker.current() != quote && tracker.current() != '\n') {
        if (tracker.current() == '\\' && tracker.peek(1) != '\0') {
            tracker.advance();
            char escaped = tracker.current();
            stringbuf_append_char(text, escaped == 'n' ? '\n' : escaped);
            tracker.advance();
        } else {
            stringbuf_append_char(text, tracker.current());
            tracker.advance();
        }
    }
    if (tracker.current() == quote) tracker.advance();
    return ctx.builder.createString(text->str->chars, text->str->len);
}

static void add_mermaid_class_note(InputContext& ctx, Element* root_graph,
                                   const char* owner_kind, const char* owner_id,
                                   String* label, const SourceLocation& source_start) {
    if (!label) return;
    Element* annotation = ctx.builder.element("annotation")
        .attr("owner-kind", owner_kind).attr("owner-id", owner_id)
        .attr("kind", "note-right").attr("label", label->chars)
        .attr("label-format", "text").final().element;
    graph_set_source_span(ctx, annotation, source_start, ctx.tracker.location(), false);
    add_node_to_graph(ctx.input(), root_graph, annotation);
}

static void add_mermaid_class_member(InputContext& ctx, Element* node,
                                     const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* value_start = tracker.rest();
    while (!tracker.atEnd() && tracker.current() != '\n' && tracker.current() != '}') {
        tracker.advance();
    }
    String* value = create_trimmed_mermaid_text(ctx, value_start,
        (size_t)(tracker.rest() - value_start), true);
    if (!value || value->len == 0) return;

    const char* kind = strstr(value->chars, "<<") == value->chars ? "stereotype"
        : strchr(value->chars, '(') ? "method" : "field";
    const char* visibility = value->chars[0] == '+' ? "public"
        : value->chars[0] == '-' ? "private"
        : value->chars[0] == '#' ? "protected"
        : value->chars[0] == '~' ? "package" : nullptr;
    size_t display_length = value->len;
    const char* classifier = nullptr;
    if (display_length > 0 && value->chars[display_length - 1] == '$') {
        classifier = "static";
        display_length--;
    } else if (strcmp(kind, "method") == 0 && display_length > 0 &&
               value->chars[display_length - 1] == '*') {
        classifier = "abstract";
        display_length--;
    }
    String* display = mermaid_class_display(ctx, value->chars, display_length);
    ElementBuilder member = ctx.builder.element("class-member");
    member.attr("kind", kind).attr("value", value->chars).attr("display", display->chars);
    if (visibility) member.attr("visibility", visibility);
    if (classifier) member.attr("classifier", classifier);
    Element* result = member.final().element;
    graph_set_source_span(ctx, result, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), node, result);
}

static void parse_mermaid_class_declaration(InputContext& ctx, Element* graph,
                                             Element* root_graph,
                                             const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    while (!tracker.atEnd()) {
        MermaidClassRef ref = parse_mermaid_class_ref(ctx);
        if (!ref.id) {
            ctx.addErrorCode(source_start, "mermaid.class.missing-id",
                "Expected class identifier after 'class'");
            skip_to_eol(tracker);
            return;
        }

        const char* shape = "box";
        const char* label_format = "text";
        String* label = ref.display;
        skip_inline_whitespace(tracker);
        if (tracker.current() == '[') {
            label = parse_mermaid_node_shape(ctx, ref.id->chars, &shape, &label_format);
        }
        Element* node = add_mermaid_node_declaration(ctx, graph, ref.id->chars,
            label ? label->chars : ref.id->chars, "box", label_format);
        add_graph_attribute(ctx.input(), node, "mermaid-family", "class");
        if (ref.generic) add_graph_attribute(ctx.input(), node, "generic", ref.generic->chars);
        skip_inline_whitespace(tracker);
        if (tracker.match("<<")) {
            add_mermaid_class_annotation(ctx, node, parse_mermaid_class_annotation(ctx),
                                          source_start);
        }
        parse_mermaid_class_suffix(ctx, graph, ref.id->chars);
        skip_inline_whitespace(tracker);

        if (tracker.current() == '{') {
            tracker.advance();
            while (!tracker.atEnd()) {
                skip_whitespace_and_comments_mermaid(tracker);
                if (tracker.current() == '}') {
                    tracker.advance();
                    break;
                }
                SourceLocation member_start = tracker.location();
                if (consume_keyword(tracker, "note")) {
                    add_mermaid_class_note(ctx, root_graph, "node", ref.id->chars,
                        parse_mermaid_class_note_text(ctx), member_start);
                } else {
                    add_mermaid_class_member(ctx, node, member_start);
                }
                if (tracker.current() == '\n') tracker.advance();
            }
            graph_set_source_span(ctx, node, source_start, tracker.location(), false);
            return;
        }
        graph_set_source_span(ctx, node, source_start, tracker.location(), false);
        skip_inline_whitespace(tracker);
        if (tracker.current() != ',') {
            skip_to_eol(tracker);
            return;
        }
        tracker.advance();
    }
}

static void parse_mermaid_class_stereotype(InputContext& ctx, Element* graph,
                                            Element* root_graph,
                                            const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* value = parse_mermaid_class_annotation(ctx);
    MermaidClassRef ref = parse_mermaid_class_ref(ctx);
    if (!ref.id) {
        ctx.addErrorCode(source_start, "mermaid.class.missing-id",
            "Expected class identifier after stereotype");
        skip_to_eol(tracker);
        return;
    }
    Element* node = ensure_mermaid_class_node(ctx, graph, root_graph, ref);
    add_mermaid_class_annotation(ctx, node, value, source_start);
    graph_set_source_span(ctx, node, source_start, tracker.location(), true);
    skip_to_eol(tracker);
}

static void parse_mermaid_class_relation(InputContext& ctx, Element* graph,
                                          Element* root_graph,
                                          const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    MermaidClassRef from = parse_mermaid_class_ref(ctx);
    if (!from.id) {
        ctx.addErrorCode(source_start, "mermaid.class.statement",
            "Expected class declaration or relationship");
        skip_to_eol(tracker);
        return;
    }
    skip_inline_whitespace(tracker);
    if (tracker.current() == ':') {
        tracker.advance();
        Element* node = ensure_mermaid_class_node(ctx, graph, root_graph, from);
        add_mermaid_class_member(ctx, node, source_start);
        graph_set_source_span(ctx, node, source_start, tracker.location(), true);
        return;
    }

    String* from_cardinality = tracker.current() == '"'
        ? parse_mermaid_interaction_token(ctx) : nullptr;
    skip_inline_whitespace(tracker);
    MermaidClassRelation relation = {};
    if (!consume_mermaid_class_relation(ctx, &relation)) {
        ctx.addErrorCode(source_start, "mermaid.class.relationship",
            "Expected a supported Mermaid class relationship after '%s'", from.id->chars);
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    String* to_cardinality = tracker.current() == '"'
        ? parse_mermaid_interaction_token(ctx) : nullptr;
    MermaidClassRef to = parse_mermaid_class_ref(ctx);
    if (!to.id) {
        ctx.addErrorCode(source_start, "mermaid.class.missing-endpoint",
            "Expected class identifier after relationship '%s'", relation.token->chars);
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    String* label = nullptr;
    const char* label_format = "text";
    if (tracker.current() == ':') {
        tracker.advance();
        String* raw = parse_mermaid_line_text(ctx, true);
        label = normalize_mermaid_label(ctx, raw->chars, raw->len, &label_format);
    }

    ensure_mermaid_class_node(ctx, graph, root_graph, from);
    ensure_mermaid_class_node(ctx, graph, root_graph, to);
    Element* edge = create_edge_element(ctx.input(), from.id->chars, to.id->chars,
        label ? label->chars : nullptr, relation.style,
        strcmp(relation.marker_start, "none") != 0 ? "true" : "false",
        strcmp(relation.marker_end, "none") != 0 ? "true" : "false");
    add_graph_attribute(ctx.input(), edge, "relation", relation.token->chars);
    add_graph_attribute(ctx.input(), edge, "arrow-tail", relation.marker_start);
    add_graph_attribute(ctx.input(), edge, "arrow-head", relation.marker_end);
    if (label) add_graph_attribute(ctx.input(), edge, "label-format", label_format);
    if (from_cardinality) {
        add_graph_attribute(ctx.input(), edge, "from-cardinality", from_cardinality->chars);
    }
    if (to_cardinality) {
        add_graph_attribute(ctx.input(), edge, "to-cardinality", to_cardinality->chars);
    }
    graph_set_source_span(ctx, edge, source_start, tracker.location(), false);
    add_edge_to_graph(ctx.input(), graph, edge);
}

static void parse_mermaid_class_note(InputContext& ctx, Element* graph, Element* root_graph,
                                     const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* owner_kind = "graph";
    const char* owner_id = "__graph__";
    MermaidClassRef target = {};
    if (consume_keyword(tracker, "for")) {
        target = parse_mermaid_class_ref(ctx);
        if (!target.id) {
            ctx.addErrorCode(source_start, "mermaid.class.note-target",
                "Expected a class identifier after 'note for'");
            skip_to_eol(tracker);
            return;
        }
        ensure_mermaid_class_node(ctx, graph, root_graph, target);
        owner_kind = "node";
        owner_id = target.id->chars;
    }
    add_mermaid_class_note(ctx, root_graph, owner_kind, owner_id,
                            parse_mermaid_class_note_text(ctx), source_start);
    skip_to_eol(tracker);
}

static Element* parse_mermaid_class_namespace_path(InputContext& ctx, Element* graph,
                                                    const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* path_start = tracker.rest();
    while (!tracker.atEnd() && !str_char_is_ascii_space(tracker.current()) &&
           tracker.current() != '[' && tracker.current() != '{' && tracker.current() != ';') {
        tracker.advance();
    }
    String* path = ctx.builder.createString(path_start, (size_t)(tracker.rest() - path_start));
    if (!path || path->len == 0) {
        ctx.addErrorCode(source_start, "mermaid.class.namespace-id",
            "Expected a namespace identifier");
        return nullptr;
    }

    Element* container = graph;
    const char* cursor = path->chars;
    const char* end = path->chars + path->len;
    while (cursor < end) {
        const char* segment_end = cursor;
        while (segment_end < end && *segment_end != '.') segment_end++;
        String* segment = ctx.builder.createString(cursor, (size_t)(segment_end - cursor));
        const char* parent_id = ElementReader(container).get_attr_string("id");
        StringBuf* id = ctx.sb;
        stringbuf_reset(id);
        if (parent_id && *parent_id) {
            stringbuf_append_str(id, parent_id);
            stringbuf_append_char(id, '.');
        }
        stringbuf_append_str_n(id, segment->chars, segment->len);
        String* namespace_id = ctx.builder.createString(id->str->chars, id->str->len);
        Element* child = find_direct_mermaid_child(container, "subgraph", namespace_id->chars);
        if (!child) {
            child = create_cluster_element(ctx.input(), namespace_id->chars, segment->chars);
            add_graph_attribute(ctx.input(), child, "namespace", "true");
            add_graph_attribute(ctx.input(), child, "mermaid-family", "class");
            add_node_to_graph(ctx.input(), container, child);
        }
        container = child;
        cursor = segment_end < end ? segment_end + 1 : end;
    }
    return container;
}

static void parse_mermaid_class_statements(InputContext& ctx, Element* graph,
                                           Element* root_graph, bool stop_at_brace);

static void parse_mermaid_class_namespace(InputContext& ctx, Element* graph,
                                          Element* root_graph,
                                          const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    Element* container = parse_mermaid_class_namespace_path(ctx, graph, source_start);
    if (!container) {
        skip_to_eol(tracker);
        return;
    }
    skip_inline_whitespace(tracker);
    if (tracker.current() == '[') {
        const char* shape = "box";
        const char* format = "text";
        const char* id = ElementReader(container).get_attr_string("id");
        String* label = parse_mermaid_node_shape(ctx, id, &shape, &format);
        if (label) add_graph_attribute(ctx.input(), container, "label", label->chars);
    }
    skip_inline_whitespace(tracker);
    if (tracker.current() != '{') {
        ctx.addErrorCode(source_start, "mermaid.class.namespace-open",
            "Expected '{' after namespace identifier");
        skip_to_eol(tracker);
        return;
    }
    tracker.advance();
    parse_mermaid_class_statements(ctx, container, root_graph, true);
    graph_set_source_span(ctx, container, source_start, tracker.location(), true);
}

static void parse_mermaid_class_statements(InputContext& ctx, Element* graph,
                                           Element* root_graph, bool stop_at_brace) {
    SourceTracker& tracker = ctx.tracker;
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.atEnd()) break;
        if (stop_at_brace && tracker.current() == '}') {
            tracker.advance();
            return;
        }
        SourceLocation statement_start = tracker.location();

        if (consume_keyword(tracker, "direction")) {
            parse_mermaid_family_direction(ctx, root_graph, statement_start,
                "mermaid.class.direction", "class");
        } else if (consume_keyword(tracker, "namespace")) {
            parse_mermaid_class_namespace(ctx, graph, root_graph, statement_start);
        } else if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_declaration(ctx, graph, root_graph, statement_start);
        } else if (tracker.match("<<")) {
            parse_mermaid_class_stereotype(ctx, graph, root_graph, statement_start);
        } else if (consume_keyword(tracker, "note")) {
            parse_mermaid_class_note(ctx, graph, root_graph, statement_start);
        } else if (consume_keyword(tracker, "accTitle")) {
            parse_mermaid_accessibility(ctx, root_graph, statement_start, "title", false);
        } else if (consume_keyword(tracker, "accDescr")) {
            parse_mermaid_accessibility(ctx, root_graph, statement_start, "description", true);
        } else if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "cssClass")) {
            parse_mermaid_class_def(ctx, graph, statement_start, true);
        } else if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, false);
        } else if (consume_keyword(tracker, "click")) {
            parse_mermaid_interaction(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "link")) {
            parse_mermaid_interaction(ctx, graph, statement_start, "link");
        } else if (consume_keyword(tracker, "callback")) {
            parse_mermaid_interaction(ctx, graph, statement_start, "callback");
        } else {
            parse_mermaid_class_relation(ctx, graph, root_graph, statement_start);
        }

        if (tracker.current() == '\n') tracker.advance();
    }
    if (stop_at_brace) {
        ctx.addErrorCode(tracker.location(), "mermaid.class.namespace-close",
            "Expected '}' to close namespace");
    }
}

static void parse_mermaid_class_diagram(InputContext& ctx, Element* graph) {
    parse_mermaid_class_statements(ctx, graph, graph, false);
}

struct MermaidErEntityRef {
    String* id;
    String* label;
    const char* label_format;
    bool alias;
};

static bool parse_mermaid_er_entity_ref(InputContext& ctx, MermaidErEntityRef* entity) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    memset(entity, 0, sizeof(*entity));
    entity->label_format = "text";
    if (tracker.current() == '"') {
        entity->id = parse_mermaid_interaction_token(ctx);
        entity->label = entity->id;
        entity->label_format = "markdown";
        return entity->id != nullptr;
    }

    entity->id = parse_mermaid_identifier(ctx);
    if (!entity->id) return false;
    entity->label = entity->id;
    skip_inline_whitespace(tracker);
    if (tracker.current() == '[') {
        const char* shape = "box";
        entity->label = parse_mermaid_node_shape(ctx, entity->id->chars,
            &shape, &entity->label_format);
        entity->alias = entity->label != nullptr;
    }
    return true;
}

static Element* materialize_mermaid_er_entity(InputContext& ctx, Element* graph,
                                               const MermaidErEntityRef& entity,
                                               bool declaration,
                                               const SourceLocation& source_start) {
    Element* node = declaration || entity.alias
        ? add_mermaid_node_declaration(ctx, graph, entity.id->chars,
            entity.label ? entity.label->chars : entity.id->chars, "box", entity.label_format)
        : ensure_mermaid_node(ctx, graph, graph, entity.id->chars);
    add_graph_attribute(ctx.input(), node, "mermaid-family", "er");
    if (declaration || entity.alias) {
        graph_set_source_span(ctx, node, source_start, ctx.tracker.location(), false);
    }
    return node;
}

static String* parse_mermaid_er_attribute_word(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* start = tracker.rest();
    while (!tracker.atEnd() && !str_char_is_ascii_space(tracker.current()) &&
           tracker.current() != '}') {
        tracker.advance();
    }
    size_t length = (size_t)(tracker.rest() - start);
    return length > 0 ? ctx.builder.createString(start, length) : nullptr;
}

static void add_mermaid_er_attribute(InputContext& ctx, Element* node,
                                     const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* type = parse_mermaid_er_attribute_word(ctx);
    String* name = parse_mermaid_er_attribute_word(ctx);
    if (!type || !name) {
        ctx.addErrorCode(source_start, "mermaid.er.attribute",
            "Expected an entity attribute type and name");
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    String* key = nullptr;
    if (tracker.current() != '"' && tracker.current() != '\n' && tracker.current() != '}') {
        const char* key_start = tracker.rest();
        while (!tracker.atEnd() && tracker.current() != '"' &&
               tracker.current() != '\n' && tracker.current() != '}') {
            tracker.advance();
        }
        key = create_trimmed_mermaid_text(ctx, key_start,
            (size_t)(tracker.rest() - key_start), true);
    }
    skip_inline_whitespace(tracker);
    String* comment = tracker.current() == '"' ? parse_mermaid_interaction_token(ctx) : nullptr;

    ElementBuilder attribute = ctx.builder.element("er-attribute");
    attribute.attr("type", type->chars).attr("name", name->chars);
    if (key && key->len > 0) attribute.attr("key", key->chars);
    if (comment) attribute.attr("comment", comment->chars);
    Element* result = attribute.final().element;
    graph_set_source_span(ctx, result, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), node, result);
    skip_to_eol(tracker);
}

static bool mermaid_er_cardinality(const char pair[2], const char** value,
                                    const char** marker) {
    bool bar0 = pair[0] == '|';
    bool bar1 = pair[1] == '|';
    bool zero = pair[0] == 'o' || pair[1] == 'o';
    bool many = pair[0] == '{' || pair[0] == '}' ||
        pair[1] == '{' || pair[1] == '}';
    if (bar0 && bar1) {
        *value = "exactly-one";
        *marker = "tee tee";
    } else if (zero && (bar0 || bar1)) {
        *value = "zero-or-one";
        *marker = "odot tee";
    } else if (zero && many) {
        *value = "zero-or-more";
        *marker = "odot crow";
    } else if (many && (bar0 || bar1)) {
        *value = "one-or-more";
        *marker = "tee crow";
    } else {
        return false;
    }
    return true;
}

static bool consume_mermaid_er_relation(InputContext& ctx,
                                         const char** from_cardinality,
                                         const char** to_cardinality,
                                         const char** marker_start,
                                         const char** marker_end,
                                         const char** style,
                                         const char** relation) {
    SourceTracker& tracker = ctx.tracker;
    char from_pair[2] = {tracker.current(), tracker.peek(1)};
    char to_pair[2] = {tracker.peek(4), tracker.peek(5)};
    bool solid = tracker.peek(2) == '-' && tracker.peek(3) == '-';
    bool dotted = tracker.peek(2) == '.' && tracker.peek(3) == '.';
    if ((!solid && !dotted) ||
        !mermaid_er_cardinality(from_pair, from_cardinality, marker_start) ||
        !mermaid_er_cardinality(to_pair, to_cardinality, marker_end)) {
        return false;
    }
    *style = solid ? "solid" : "dotted";
    *relation = ctx.builder.createString(tracker.rest(), 6)->chars;
    tracker.advance(6);
    return true;
}

static void parse_mermaid_er_statement(InputContext& ctx, Element* graph,
                                       const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    MermaidErEntityRef from;
    if (!parse_mermaid_er_entity_ref(ctx, &from)) {
        ctx.addErrorCode(source_start, "mermaid.er.entity", "Expected an ER entity name");
        skip_to_eol(tracker);
        return;
    }
    parse_mermaid_class_suffix(ctx, graph, from.id->chars);
    skip_inline_whitespace(tracker);
    if (tracker.current() == '{') {
        tracker.advance();
        Element* node = materialize_mermaid_er_entity(ctx, graph, from, true, source_start);
        while (!tracker.atEnd()) {
            skip_whitespace_and_comments_mermaid(tracker);
            if (tracker.current() == '}') {
                tracker.advance();
                break;
            }
            add_mermaid_er_attribute(ctx, node, tracker.location());
        }
        graph_set_source_span(ctx, node, source_start, tracker.location(), false);
        return;
    }
    if (tracker.atEnd() || tracker.current() == '\n' || tracker.current() == ';') {
        materialize_mermaid_er_entity(ctx, graph, from, true, source_start);
        skip_to_eol(tracker);
        return;
    }

    const char* from_cardinality = nullptr;
    const char* to_cardinality = nullptr;
    const char* marker_start = nullptr;
    const char* marker_end = nullptr;
    const char* style = nullptr;
    const char* relation = nullptr;
    if (!consume_mermaid_er_relation(ctx, &from_cardinality, &to_cardinality,
                                     &marker_start, &marker_end, &style, &relation)) {
        ctx.addErrorCode(source_start, "mermaid.er.relationship",
            "Expected a symbolic ER relationship after '%s'", from.id->chars);
        skip_to_eol(tracker);
        return;
    }

    MermaidErEntityRef to;
    if (!parse_mermaid_er_entity_ref(ctx, &to)) {
        ctx.addErrorCode(source_start, "mermaid.er.missing-endpoint",
            "Expected an entity after relationship '%s'", relation);
        skip_to_eol(tracker);
        return;
    }
    parse_mermaid_class_suffix(ctx, graph, to.id->chars);
    skip_inline_whitespace(tracker);
    String* label = nullptr;
    const char* label_format = "text";
    if (tracker.current() == ':') {
        tracker.advance();
        String* raw = parse_mermaid_line_text(ctx, true);
        label = normalize_mermaid_label(ctx, raw->chars, raw->len, &label_format);
    } else {
        ctx.addErrorCode(source_start, "mermaid.er.missing-label",
            "Expected ':' and an ER relationship label");
        skip_to_eol(tracker);
    }

    materialize_mermaid_er_entity(ctx, graph, from, false, source_start);
    materialize_mermaid_er_entity(ctx, graph, to, false, source_start);
    Element* edge = create_edge_element(ctx.input(), from.id->chars, to.id->chars,
        label ? label->chars : nullptr, style, "true", "true");
    add_graph_attribute(ctx.input(), edge, "relation", relation);
    add_graph_attribute(ctx.input(), edge, "identifying",
        strcmp(style, "solid") == 0 ? "true" : "false");
    add_graph_attribute(ctx.input(), edge, "from-cardinality", from_cardinality);
    add_graph_attribute(ctx.input(), edge, "to-cardinality", to_cardinality);
    add_graph_attribute(ctx.input(), edge, "arrow-tail", marker_start);
    add_graph_attribute(ctx.input(), edge, "arrow-head", marker_end);
    if (label) add_graph_attribute(ctx.input(), edge, "label-format", label_format);
    graph_set_source_span(ctx, edge, source_start, tracker.location(), false);
    add_edge_to_graph(ctx.input(), graph, edge);
}

static void parse_mermaid_er_diagram(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.atEnd()) break;
        SourceLocation statement_start = tracker.location();
        if (consume_keyword(tracker, "direction")) {
            parse_mermaid_family_direction(ctx, graph, statement_start,
                "mermaid.er.direction", "ER");
        } else if (consume_keyword(tracker, "accTitle")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "title", false);
        } else if (consume_keyword(tracker, "accDescr")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "description", true);
        } else if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, false);
        } else {
            parse_mermaid_er_statement(ctx, graph, statement_start);
        }
        if (tracker.current() == '\n') tracker.advance();
    }
}

struct MermaidStateEndpoint {
    String* id;
    const char* kind;
};

static bool parse_mermaid_state_endpoint(InputContext& ctx, bool source,
                                          MermaidStateEndpoint* endpoint) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    endpoint->kind = "state";
    if (tracker.match("[*]")) {
        tracker.advance(3);
        endpoint->id = ctx.builder.createString(source ? "__state_start" : "__state_end");
        endpoint->kind = source ? "start" : "end";
        return true;
    }
    endpoint->id = parse_mermaid_identifier(ctx);
    return endpoint->id != nullptr;
}

static Element* ensure_mermaid_state_node(InputContext& ctx, Element* graph,
                                           const MermaidStateEndpoint& endpoint) {
    Element* node = find_mermaid_node(graph, endpoint.id->chars);
    bool created = node == nullptr;
    if (!node) {
        const char* shape = strcmp(endpoint.kind, "start") == 0 ? "circle"
            : strcmp(endpoint.kind, "end") == 0 ? "doublecircle" : "rounded";
        const char* label = strcmp(endpoint.kind, "state") == 0 ? endpoint.id->chars : "";
        node = create_node_element(ctx.input(), endpoint.id->chars, label, shape);
        add_node_to_graph(ctx.input(), graph, node);
    }
    add_graph_attribute(ctx.input(), node, "mermaid-family", "state");
    // ordinary transition references must not erase an explicitly declared
    // choice/fork/join role on the existing state node.
    if (created || strcmp(endpoint.kind, "state") != 0) {
        add_graph_attribute(ctx.input(), node, "state-kind", endpoint.kind);
    }
    return node;
}

static Element* add_mermaid_state_declaration(InputContext& ctx, Element* graph,
                                               const char* id, const char* label,
                                               const char* shape, const char* kind,
                                               const char* label_format,
                                               const SourceLocation& source_start) {
    Element* node = add_mermaid_node_declaration(ctx, graph, id, label, shape, label_format);
    add_graph_attribute(ctx.input(), node, "mermaid-family", "state");
    add_graph_attribute(ctx.input(), node, "state-kind", kind);
    graph_set_source_span(ctx, node, source_start, ctx.tracker.location(), false);
    return node;
}

static void add_mermaid_state_description(InputContext& ctx, Element* graph, String* id,
                                           const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    tracker.advance();
    String* raw = parse_mermaid_line_text(ctx, true);
    const char* format = "text";
    String* value = normalize_mermaid_label(ctx, raw->chars, raw->len, &format);
    Element* node = add_mermaid_state_declaration(ctx, graph, id->chars, id->chars,
        "rounded", "state", "text", source_start);
    Element* description = ctx.builder.element("state-description")
        .attr("value", value->chars).attr("label-format", format).final().element;
    graph_set_source_span(ctx, description, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), node, description);
    graph_set_source_span(ctx, node, source_start, tracker.location(), false);
}

static void skip_mermaid_state_composite(InputContext& ctx,
                                          const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    int depth = 1;
    tracker.advance();
    while (!tracker.atEnd() && depth > 0) {
        if (tracker.current() == '{') depth++;
        else if (tracker.current() == '}') depth--;
        tracker.advance();
    }
    ctx.addErrorCode(source_start, "mermaid.state.composite-unsupported",
        "Composite Mermaid states require the cluster-aware state tranche");
}

static void parse_mermaid_state_declaration(InputContext& ctx, Element* graph,
                                             const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* id = nullptr;
    String* label = nullptr;
    const char* label_format = "text";
    skip_inline_whitespace(tracker);
    if (tracker.current() == '"') {
        label = parse_mermaid_interaction_token(ctx);
        label_format = "markdown";
        skip_inline_whitespace(tracker);
        if (!consume_keyword(tracker, "as")) {
            ctx.addErrorCode(source_start, "mermaid.state.missing-as",
                "Expected 'as' after a quoted state description");
        }
        id = parse_mermaid_identifier(ctx);
    } else {
        id = parse_mermaid_identifier(ctx);
        label = id;
    }
    if (!id) {
        ctx.addErrorCode(source_start, "mermaid.state.missing-id",
            "Expected a state identifier");
        skip_to_eol(tracker);
        return;
    }

    parse_mermaid_class_suffix(ctx, graph, id->chars);
    skip_inline_whitespace(tracker);
    const char* kind = "state";
    const char* shape = "rounded";
    if (tracker.match("<<")) {
        tracker.advance(2);
        String* annotation = parse_mermaid_identifier(ctx);
        if (tracker.match(">>")) tracker.advance(2);
        if (annotation && strcmp(annotation->chars, "choice") == 0) {
            kind = "choice";
            shape = "diamond";
            label = ctx.builder.createString("");
        } else if (annotation && (strcmp(annotation->chars, "fork") == 0 ||
                   strcmp(annotation->chars, "join") == 0)) {
            kind = annotation->chars;
            shape = "box";
            label = ctx.builder.createString("");
        } else {
            ctx.addErrorCode(source_start, "mermaid.state.annotation",
                "Expected state annotation choice, fork, or join");
        }
    }

    Element* node = add_mermaid_state_declaration(ctx, graph, id->chars,
        label ? label->chars : id->chars, shape, kind, label_format, source_start);
    skip_inline_whitespace(tracker);
    if (tracker.current() == '{') {
        add_graph_attribute(ctx.input(), node, "composite", "true");
        skip_mermaid_state_composite(ctx, source_start);
    } else {
        skip_to_eol(tracker);
    }
    graph_set_source_span(ctx, node, source_start, tracker.location(), false);
}

static void parse_mermaid_state_transition_or_description(
        InputContext& ctx, Element* graph, const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    MermaidStateEndpoint from;
    if (!parse_mermaid_state_endpoint(ctx, true, &from)) {
        ctx.addErrorCode(source_start, "mermaid.state.statement",
            "Expected a state declaration or transition");
        skip_to_eol(tracker);
        return;
    }
    if (strcmp(from.kind, "state") == 0) {
        parse_mermaid_class_suffix(ctx, graph, from.id->chars);
    }
    skip_inline_whitespace(tracker);
    if (tracker.current() == ':') {
        add_mermaid_state_description(ctx, graph, from.id, source_start);
        return;
    }
    if (tracker.current() == '\n' || tracker.current() == ';' || tracker.atEnd()) {
        add_mermaid_state_declaration(ctx, graph, from.id->chars, from.id->chars,
            "rounded", "state", "text", source_start);
        skip_to_eol(tracker);
        return;
    }
    if (!tracker.match("-->")) {
        ctx.addErrorCode(source_start, "mermaid.state.transition",
            "Expected '-->' after state '%s'", from.id->chars);
        skip_to_eol(tracker);
        return;
    }
    tracker.advance(3);
    MermaidStateEndpoint to;
    if (!parse_mermaid_state_endpoint(ctx, false, &to)) {
        ctx.addErrorCode(source_start, "mermaid.state.missing-endpoint",
            "Expected a state after '-->'");
        skip_to_eol(tracker);
        return;
    }
    if (strcmp(to.kind, "state") == 0) parse_mermaid_class_suffix(ctx, graph, to.id->chars);
    skip_inline_whitespace(tracker);
    String* label = nullptr;
    const char* label_format = "text";
    if (tracker.current() == ':') {
        tracker.advance();
        String* raw = parse_mermaid_line_text(ctx, true);
        label = normalize_mermaid_label(ctx, raw->chars, raw->len, &label_format);
    } else {
        skip_to_eol(tracker);
    }

    Element* from_node = ensure_mermaid_state_node(ctx, graph, from);
    Element* to_node = ensure_mermaid_state_node(ctx, graph, to);
    graph_set_source_span(ctx, from_node, source_start, tracker.location(), true);
    graph_set_source_span(ctx, to_node, source_start, tracker.location(), true);
    Element* edge = create_edge_element(ctx.input(), from.id->chars, to.id->chars,
        label ? label->chars : nullptr, "solid", "false", "true");
    add_graph_attribute(ctx.input(), edge, "relation", "-->");
    add_graph_attribute(ctx.input(), edge, "arrow-tail", "none");
    add_graph_attribute(ctx.input(), edge, "arrow-head", "normal");
    if (label) add_graph_attribute(ctx.input(), edge, "label-format", label_format);
    graph_set_source_span(ctx, edge, source_start, tracker.location(), false);
    add_edge_to_graph(ctx.input(), graph, edge);
}

static String* parse_mermaid_state_note_text(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    if (tracker.current() == ':') {
        tracker.advance();
        return parse_mermaid_line_text(ctx, true);
    }
    if (tracker.current() == '\n') tracker.advance();
    StringBuf* note = ctx.sb;
    stringbuf_reset(note);
    while (!tracker.atEnd()) {
        skip_inline_whitespace(tracker);
        if (tracker.match("end note")) {
            tracker.advance(8);
            break;
        }
        const char* line_start = tracker.rest();
        while (!tracker.atEnd() && tracker.current() != '\n') tracker.advance();
        String* line = create_trimmed_mermaid_text(ctx, line_start,
            (size_t)(tracker.rest() - line_start), false);
        if (line->len > 0) {
            if (note->str->len > 0) stringbuf_append_char(note, '\n');
            stringbuf_append_str_n(note, line->chars, line->len);
        }
        if (tracker.current() == '\n') tracker.advance();
    }
    return ctx.builder.createString(note->str->chars, note->str->len);
}

static void parse_mermaid_state_note(InputContext& ctx, Element* graph,
                                     const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_inline_whitespace(tracker);
    const char* side = consume_keyword(tracker, "left") ? "note-left"
        : consume_keyword(tracker, "right") ? "note-right" : nullptr;
    skip_inline_whitespace(tracker);
    if (!side || !consume_keyword(tracker, "of")) {
        ctx.addErrorCode(source_start, "mermaid.state.note-position",
            "Expected 'note left of' or 'note right of'");
        skip_to_eol(tracker);
        return;
    }
    String* target = parse_mermaid_identifier(ctx);
    if (!target) {
        ctx.addErrorCode(source_start, "mermaid.state.note-target",
            "Expected a note target state");
        skip_to_eol(tracker);
        return;
    }
    String* raw = parse_mermaid_state_note_text(ctx);
    const char* format = "text";
    String* label = normalize_mermaid_label(ctx, raw->chars, raw->len, &format);
    MermaidStateEndpoint endpoint = {target, "state"};
    ensure_mermaid_state_node(ctx, graph, endpoint);
    Element* annotation = ctx.builder.element("annotation")
        .attr("owner-kind", "node").attr("owner-id", target->chars)
        .attr("kind", side).attr("label", label->chars)
        .attr("label-format", format).final().element;
    graph_set_source_span(ctx, annotation, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, annotation);
}

static void parse_mermaid_state_diagram(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.atEnd()) break;
        SourceLocation statement_start = tracker.location();
        if (consume_keyword(tracker, "direction")) {
            parse_mermaid_family_direction(ctx, graph, statement_start,
                "mermaid.state.direction", "state");
        } else if (consume_keyword(tracker, "state")) {
            parse_mermaid_state_declaration(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "note")) {
            parse_mermaid_state_note(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "accTitle")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "title", false);
        } else if (consume_keyword(tracker, "accDescr")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "description", true);
        } else if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, false);
        } else if (consume_keyword(tracker, "hide")) {
            ctx.addErrorCode(statement_start, "mermaid.state.hide-unsupported",
                "State description hiding is not implemented");
            skip_to_eol(tracker);
        } else {
            parse_mermaid_state_transition_or_description(ctx, graph, statement_start);
        }
        if (tracker.current() == '\n') tracker.advance();
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
    SourceLocation graph_start = tracker.location();
    SourceLocation front_matter_end = graph_start;
    String* front_matter = parse_mermaid_front_matter(ctx, &front_matter_end);
    if (front_matter) skip_whitespace_and_comments_mermaid(tracker);
    SourceLocation init_start = tracker.location();
    SourceLocation init_end = init_start;
    String* init_directive = parse_mermaid_init_directive(ctx, &init_end);
    if (init_directive) skip_whitespace_and_comments_mermaid(tracker);

    const char* diagram_type = "flowchart";
    char direction[3] = {'T', 'B', '\0'};
    bool unsupported_chart = false;
    void (*family_parser)(InputContext&, Element*) = nullptr;
    const char* family_fallback = nullptr;

    if (consume_keyword(tracker, "graph") || consume_keyword(tracker, "flowchart")) {
        parse_mermaid_direction(tracker, direction);
    } else if (consume_keyword(tracker, "classDiagram")) {
        diagram_type = "class";
        family_parser = parse_mermaid_class_diagram;
        family_fallback = "mermaid.class.syntax";
    } else if (consume_keyword(tracker, "erDiagram")) {
        diagram_type = "er";
        family_parser = parse_mermaid_er_diagram;
        family_fallback = "mermaid.er.syntax";
    } else if (consume_keyword(tracker, "stateDiagram-v2") ||
               consume_keyword(tracker, "stateDiagram")) {
        diagram_type = "state";
        family_parser = parse_mermaid_state_diagram;
        family_fallback = "mermaid.state.syntax";
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
    add_graph_attribute(input, graph, "ir-stage", "source");
    add_graph_attribute(input, graph, "kind", diagram_type);
    add_graph_attribute(input, graph, "diagram-type", diagram_type);
    add_graph_attribute(input, graph, "directed", "true");
    add_graph_attribute(input, graph, "direction", normalize_direction(direction));
    add_graph_attribute(input, graph, "rank-dir", normalize_direction(direction));
    if (front_matter) {
        Element* metadata = ctx.builder.element("front-matter")
            .attr("value", front_matter->chars)
            .final().element;
        graph_set_source_span(ctx, metadata, graph_start, front_matter_end, false);
        add_node_to_graph(input, graph, metadata);
    }
    if (init_directive) {
        Element* metadata = ctx.builder.element("init")
            .attr("value", init_directive->chars)
            .final().element;
        graph_set_source_span(ctx, metadata, init_start, init_end, false);
        add_node_to_graph(input, graph, metadata);
    }

    if (unsupported_chart) {
        // Chart-oriented Mermaid families have different semantic models; treating
        // their statements as graph nodes silently corrupts the common Graph IR.
        add_graph_attribute(input, graph, "status", "unsupported");
        ctx.addErrorCode(graph_start, "mermaid.chart-family",
            "Mermaid %s diagrams belong to lambda.package.chart", diagram_type);
        graph_set_source_span(ctx, graph, graph_start, tracker.location(), false);
        graph_append_diagnostics(ctx, graph, "mermaid.syntax");
        input->root = {.element = graph};
        ctx.logErrors();
        return;
    }

    if (family_parser) {
        family_parser(ctx, graph);
        graph_set_source_span(ctx, graph, graph_start, tracker.location(), false);
        graph_append_diagnostics(ctx, graph, family_fallback);
        input->root = {.element = graph};
        if (ctx.hasErrors()) ctx.logErrors();
        return;
    }

    // parse diagram content
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);

        if (tracker.atEnd()) {
            break;
        }

        const char* line_start = tracker.rest();
        SourceLocation statement_start = tracker.location();

        if (consume_keyword(tracker, "accTitle")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "title", false);
            continue;
        }

        if (consume_keyword(tracker, "accDescr")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "description", true);
            continue;
        }

        // check for classDef
        if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph, statement_start);
            continue;
        }

        // check for class assignments
        if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_def(ctx, graph, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "click")) {
            parse_mermaid_interaction(ctx, graph, statement_start);
            continue;
        }

        if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, false);
            continue;
        }

        if (consume_keyword(tracker, "linkStyle")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, true);
            continue;
        }

        if (mermaid_property_target_length(tracker) > 0) {
            size_t target_length = mermaid_property_target_length(tracker);
            String* target = ctx.builder.createString(tracker.rest(), target_length);
            if (find_mermaid_edge(graph, target->chars)) {
                parse_mermaid_edge_properties(ctx, graph, graph, statement_start);
                continue;
            }
        }

        // check for subgraph
        if (consume_keyword(tracker, "subgraph")) {
            parse_mermaid_subgraph(ctx, graph, graph, statement_start);
            continue;
        }

        ArrayList* potential_nodes = parse_mermaid_node_list(ctx, graph, graph);
        if (potential_nodes) {
            if (is_mermaid_edge_start(tracker)) {
                parse_mermaid_edge_chain(ctx, graph, graph, potential_nodes, statement_start);
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

    graph_set_source_span(ctx, graph, graph_start, tracker.location(), false);
    graph_append_diagnostics(ctx, graph, "mermaid.syntax");

    // set graph as root
    input->root = {.element = graph};

    // report errors if any
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
