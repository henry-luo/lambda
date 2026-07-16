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
                                    const SourceLocation& source_start);
static void parse_mermaid_interaction(InputContext& ctx, Element* graph,
                                      const SourceLocation& source_start);
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
                                    const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_mermaid(tracker);

    StringBuf* targets = ctx.sb;
    stringbuf_reset(targets);
    bool all_edges = true;

    // parse node IDs (can be comma-separated)
    while (!tracker.atEnd() && !str_char_is_ascii_space(tracker.current())) {
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
        .attr("target-kind", all_edges ? "edge" : "node")
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
    String* class_name = parse_mermaid_identifier(ctx);
    if (!class_name) {
        ctx.addWarning(tracker.location(), "Expected class name after classDef");
        skip_to_eol(tracker);
        return;
    }

    String* declarations = parse_mermaid_line_text(ctx, true);
    Element* rule = ctx.builder.element("style-rule")
        .attr("class", class_name->chars)
        .attr("declarations", declarations->chars)
        .final().element;
    graph_set_source_span(ctx, rule, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), graph, rule);
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
                                      const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* target = parse_mermaid_interaction_token(ctx);
    if (!target || target->len == 0) {
        ctx.addWarning(source_start, "Expected target after Mermaid click directive");
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    const char* action = "callback";
    if (consume_keyword(tracker, "href")) action = "link";
    else if (consume_keyword(tracker, "call")) action = "callback";
    else if (tracker.current() == '"' || tracker.current() == '\'') action = "link";

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

struct MermaidClassRelation {
    const char* token;
    const char* style;
    const char* marker_start;
    const char* marker_end;
};

static const MermaidClassRelation MERMAID_CLASS_RELATIONS[] = {
    {"<|--", "solid", "empty", "none"},
    {"--|>", "solid", "none", "empty"},
    {"<|..", "dotted", "empty", "none"},
    {"..|>", "dotted", "none", "empty"},
    {"*--", "solid", "diamond", "none"},
    {"--*", "solid", "none", "diamond"},
    {"o--", "solid", "odiamond", "none"},
    {"--o", "solid", "none", "odiamond"},
    {"<--", "solid", "normal", "none"},
    {"-->", "solid", "none", "normal"},
    {"<..", "dotted", "normal", "none"},
    {"..>", "dotted", "none", "normal"},
    {"--", "solid", "none", "none"},
    {"..", "dotted", "none", "none"},
};

static const MermaidClassRelation* consume_mermaid_class_relation(SourceTracker& tracker) {
    for (size_t index = 0;
         index < sizeof(MERMAID_CLASS_RELATIONS) / sizeof(MERMAID_CLASS_RELATIONS[0]); index++) {
        const MermaidClassRelation* relation = &MERMAID_CLASS_RELATIONS[index];
        if (!tracker.match(relation->token)) continue;
        tracker.advance(strlen(relation->token));
        return relation;
    }
    return nullptr;
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
    ElementBuilder member = ctx.builder.element("class-member");
    member.attr("kind", kind).attr("value", value->chars);
    if (visibility) member.attr("visibility", visibility);
    Element* result = member.final().element;
    graph_set_source_span(ctx, result, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), node, result);
}

static void parse_mermaid_class_declaration(InputContext& ctx, Element* graph,
                                             const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* id = parse_mermaid_identifier(ctx);
    if (!id) {
        ctx.addErrorCode(source_start, "mermaid.class.missing-id",
            "Expected class identifier after 'class'");
        skip_to_eol(tracker);
        return;
    }

    const char* shape = "box";
    const char* label_format = "text";
    String* label = ctx.builder.createString(id->chars);
    skip_inline_whitespace(tracker);
    if (tracker.current() == '[') {
        label = parse_mermaid_node_shape(ctx, id->chars, &shape, &label_format);
    }
    Element* node = add_mermaid_node_declaration(ctx, graph, id->chars,
        label ? label->chars : id->chars, "box", label_format);
    add_graph_attribute(ctx.input(), node, "mermaid-family", "class");
    parse_mermaid_class_suffix(ctx, graph, id->chars);
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
            add_mermaid_class_member(ctx, node, member_start);
            if (tracker.current() == '\n') tracker.advance();
        }
    } else {
        skip_to_eol(tracker);
    }
    graph_set_source_span(ctx, node, source_start, tracker.location(), false);
}

static void parse_mermaid_class_stereotype(InputContext& ctx, Element* graph,
                                            const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    const char* value_start = tracker.rest();
    while (!tracker.atEnd() && !tracker.match(">>") && tracker.current() != '\n') {
        tracker.advance();
    }
    if (tracker.match(">>")) tracker.advance(2);
    String* value = ctx.builder.createString(value_start,
        (size_t)(tracker.rest() - value_start));
    String* id = parse_mermaid_identifier(ctx);
    if (!id) {
        ctx.addErrorCode(source_start, "mermaid.class.missing-id",
            "Expected class identifier after stereotype");
        skip_to_eol(tracker);
        return;
    }
    Element* node = ensure_mermaid_node(ctx, graph, graph, id->chars);
    Element* member = ctx.builder.element("class-member")
        .attr("kind", "stereotype").attr("value", value->chars).final().element;
    graph_set_source_span(ctx, member, source_start, tracker.location(), false);
    add_node_to_graph(ctx.input(), node, member);
    graph_set_source_span(ctx, node, source_start, tracker.location(), true);
    skip_to_eol(tracker);
}

static void parse_mermaid_class_relation(InputContext& ctx, Element* graph,
                                          const SourceLocation& source_start) {
    SourceTracker& tracker = ctx.tracker;
    String* from = parse_mermaid_identifier(ctx);
    if (!from) {
        ctx.addErrorCode(source_start, "mermaid.class.statement",
            "Expected class declaration or relationship");
        skip_to_eol(tracker);
        return;
    }
    skip_inline_whitespace(tracker);
    if (tracker.current() == ':') {
        tracker.advance();
        Element* node = ensure_mermaid_node(ctx, graph, graph, from->chars);
        add_mermaid_class_member(ctx, node, source_start);
        graph_set_source_span(ctx, node, source_start, tracker.location(), true);
        return;
    }

    String* from_cardinality = tracker.current() == '"'
        ? parse_mermaid_interaction_token(ctx) : nullptr;
    skip_inline_whitespace(tracker);
    const MermaidClassRelation* relation = consume_mermaid_class_relation(tracker);
    if (!relation) {
        ctx.addErrorCode(source_start, "mermaid.class.relationship",
            "Expected a supported Mermaid class relationship after '%s'", from->chars);
        skip_to_eol(tracker);
        return;
    }

    skip_inline_whitespace(tracker);
    String* to_cardinality = tracker.current() == '"'
        ? parse_mermaid_interaction_token(ctx) : nullptr;
    String* to = parse_mermaid_identifier(ctx);
    if (!to) {
        ctx.addErrorCode(source_start, "mermaid.class.missing-endpoint",
            "Expected class identifier after relationship '%s'", relation->token);
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

    ensure_mermaid_node(ctx, graph, graph, from->chars);
    ensure_mermaid_node(ctx, graph, graph, to->chars);
    Element* edge = create_edge_element(ctx.input(), from->chars, to->chars,
        label ? label->chars : nullptr, relation->style,
        strcmp(relation->marker_start, "none") != 0 ? "true" : "false",
        strcmp(relation->marker_end, "none") != 0 ? "true" : "false");
    add_graph_attribute(ctx.input(), edge, "relation", relation->token);
    add_graph_attribute(ctx.input(), edge, "arrow-tail", relation->marker_start);
    add_graph_attribute(ctx.input(), edge, "arrow-head", relation->marker_end);
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

static void parse_mermaid_class_diagram(InputContext& ctx, Element* graph) {
    SourceTracker& tracker = ctx.tracker;
    while (!tracker.atEnd()) {
        skip_whitespace_and_comments_mermaid(tracker);
        if (tracker.atEnd()) break;
        SourceLocation statement_start = tracker.location();

        if (consume_keyword(tracker, "direction")) {
            char direction[3] = {0};
            if (parse_mermaid_direction(tracker, direction)) {
                add_graph_attribute(ctx.input(), graph, "direction", normalize_direction(direction));
                add_graph_attribute(ctx.input(), graph, "rank-dir", normalize_direction(direction));
            } else {
                ctx.addErrorCode(statement_start, "mermaid.class.direction",
                    "Expected class diagram direction LR, RL, TB, TD, or BT");
            }
        } else if (consume_keyword(tracker, "class")) {
            parse_mermaid_class_declaration(ctx, graph, statement_start);
        } else if (tracker.match("<<")) {
            parse_mermaid_class_stereotype(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "accTitle")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "title", false);
        } else if (consume_keyword(tracker, "accDescr")) {
            parse_mermaid_accessibility(ctx, graph, statement_start, "description", true);
        } else if (consume_keyword(tracker, "classDef")) {
            parse_mermaid_style_rule(ctx, graph, statement_start);
        } else if (consume_keyword(tracker, "style")) {
            parse_mermaid_style_assignment(ctx, graph, statement_start, false);
        } else if (consume_keyword(tracker, "click")) {
            parse_mermaid_interaction(ctx, graph, statement_start);
        } else {
            parse_mermaid_class_relation(ctx, graph, statement_start);
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
    bool class_diagram = false;

    if (consume_keyword(tracker, "graph") || consume_keyword(tracker, "flowchart")) {
        parse_mermaid_direction(tracker, direction);
    } else if (consume_keyword(tracker, "classDiagram")) {
        diagram_type = "class";
        class_diagram = true;
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

    if (class_diagram) {
        parse_mermaid_class_diagram(ctx, graph);
        graph_set_source_span(ctx, graph, graph_start, tracker.location(), false);
        graph_append_diagnostics(ctx, graph, "mermaid.class.syntax");
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
