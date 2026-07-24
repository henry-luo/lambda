#include "format.h"
#include "../core/mark_reader.hpp"
#include "../../lib/escape.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"

#include <string.h>

// ============================================================================
// Escape rules (Problem 1 — already table-driven)
// ============================================================================

struct GraphEscapeRules {
    const char* trigger_chars;  // chars that require quoting
    const EscapeRule* rules;
    int rule_count;
};

static const GraphEscapeRules GRAPH_ESCAPE_DOT     = {" ->{}\"",  ESCAPE_RULES_GRAPH_DOT,    ESCAPE_RULES_GRAPH_DOT_COUNT};
static const GraphEscapeRules GRAPH_ESCAPE_MERMAID = {" -",       ESCAPE_RULES_GRAPH_QUOTED, ESCAPE_RULES_GRAPH_QUOTED_COUNT};
static const GraphEscapeRules GRAPH_ESCAPE_D2      = {" :{}\"->", ESCAPE_RULES_GRAPH_QUOTED, ESCAPE_RULES_GRAPH_QUOTED_COUNT};

static void format_graph_string(StringBuf* sb, const char* str, const char* flavor) {
    if (!str) return;
    const GraphEscapeRules* rules =
        (flavor && strcmp(flavor, "mermaid") == 0) ? &GRAPH_ESCAPE_MERMAID :
        (flavor && strcmp(flavor, "d2")      == 0) ? &GRAPH_ESCAPE_D2      :
                                                      &GRAPH_ESCAPE_DOT;
    bool needs_quotes = (strpbrk(str, rules->trigger_chars) != NULL);
    if (needs_quotes) stringbuf_append_char(sb, '"');
    escape_append_stringbuf(sb, str, strlen(str), rules->rules, rules->rule_count, ESCAPE_CTRL_NONE);
    if (needs_quotes) stringbuf_append_char(sb, '"');
}

// ============================================================================
// Syntax table (Problem 2)
// ============================================================================

struct GraphSyntax {
    const char* edge_arrow;         // " -> "  |  " --> "  |  " -> "
    const char* edge_label_sep;     // " [label="  |  " : "  |  ": "
    const char* edge_label_close;   // "]"  |  ""  |  ""
    const char* node_lbl_open;      // " [label="  |  "["  |  ": "
    const char* node_lbl_close;     // "]"  |  "]"  |  ""
    const char* stmt_end;           // ";\n"  |  "\n"  |  "\n"
    const char* node_indent;        // "    "  |  "    "  |  ""
    bool        node_needs_indent;  // true for dot/mermaid, false for d2
    // cluster
    const char* cluster_open;       // "    subgraph "  |  "    subgraph "  |  ""
    const char* cluster_id_default; // "cluster_unnamed"  |  "cluster"  |  "container"
    const char* cluster_body_open;  // " {\n"  |  "\n"  |  ": {\n"
    const char* cluster_body_close; // "    }\n"  |  "    end\n"  |  "}\n"
    const char* cluster_label_pfx;  // "        label="  |  " ["  |  "  label: "
    const char* cluster_label_sfx;  // ";\n"  |  "]\n" (mermaid label inline in open) | "\n"
    bool        cluster_label_inline; // mermaid puts label on same line as subgraph id
};

static const GraphSyntax SYNTAX_DOT = {
    " -> ", " [label=", "]",
    " [label=", "]", ";\n", "    ", true,
    "    subgraph ", "cluster_unnamed", " {\n", "    }\n",
    "        label=", ";\n", false
};
static const GraphSyntax SYNTAX_MERMAID = {
    " --> ", " : ", "",
    "[", "]", "\n", "    ", true,
    "    subgraph ", "cluster", "",  "    end\n",
    " [", "]\n", true
};
static const GraphSyntax SYNTAX_D2 = {
    " -> ", ": ", "",
    ": ", "", "\n", "", false,
    "", "container", ": {\n", "}\n",
    "  label: ", "\n", false
};

static const GraphSyntax* get_graph_syntax(const char* flavor) {
    if (flavor && strcmp(flavor, "mermaid") == 0) return &SYNTAX_MERMAID;
    if (flavor && strcmp(flavor, "d2")      == 0) return &SYNTAX_D2;
    return &SYNTAX_DOT;
}

// forward declarations (syn-based)
static void format_graph_element_with_syn(StringBuf* sb, const ElementReader& element, const char* flavor, const GraphSyntax* syn);
static void format_graph_node(StringBuf* sb, const ElementReader& node, const char* flavor, const GraphSyntax* syn);
static void format_graph_edge(StringBuf* sb, const ElementReader& edge, const char* flavor, const GraphSyntax* syn);
static void format_graph_cluster(StringBuf* sb, const ElementReader& cluster, const char* flavor, const GraphSyntax* syn);
static void format_graph_children(StringBuf* sb, const ElementReader& element, const char* flavor, const GraphSyntax* syn);

// helper to get string attribute from element
static const char* get_element_attribute(const ElementReader& elem, const char* attr_name) {
    ItemReader attr = elem.get_attr(attr_name);
    if (attr.isString()) {
        String* str = attr.asString();
        return str ? str->chars : nullptr;
    }
    return nullptr;
}

// ============================================================================
// DOT source/canonical formatter
// ============================================================================

static void format_dot_indent(StringBuf* sb, int depth) {
    for (int i = 0; i < depth; i++) stringbuf_append_str(sb, "    ");
}

static void format_dot_value(StringBuf* sb, const char* value, const char* source_kind) {
    if (!value) return;
    if (source_kind && strcmp(source_kind, "html") == 0) {
        stringbuf_append_str(sb, value);
        return;
    }
    if (source_kind && (strcmp(source_kind, "bare") == 0 ||
                        strcmp(source_kind, "numeric") == 0)) {
        stringbuf_append_str(sb, value);
        return;
    }
    stringbuf_append_char(sb, '"');
    // Graphviz label escapes are semantic input, so only quotes are escaped.
    escape_append_stringbuf(sb, value, strlen(value), ESCAPE_RULES_GRAPH_QUOTED,
                            ESCAPE_RULES_GRAPH_QUOTED_COUNT, ESCAPE_CTRL_NONE);
    stringbuf_append_char(sb, '"');
}

static void format_dot_properties_filtered(StringBuf* sb, const ElementReader& owner,
                                           bool skip_inherited) {
    auto children = owner.childElements();
    ElementReader properties;
    while (children.next(&properties)) {
        if (!properties.hasTag("properties")) continue;
        stringbuf_append_str(sb, " [");
        bool first = true;
        auto values = properties.childElements();
        ElementReader property;
        while (values.next(&property)) {
            if (!property.hasTag("property")) continue;
            const char* origin = get_element_attribute(property, "origin");
            // Canonical subgraphs inherit parent graph properties again when reparsed.
            if (skip_inherited && origin && strcmp(origin, "inherited") == 0) continue;
            const char* name = get_element_attribute(property, "name");
            const char* value = get_element_attribute(property, "value");
            if (!name || !value) continue;
            if (!first) stringbuf_append_str(sb, ", ");
            format_dot_value(sb, name, get_element_attribute(property, "name-source-kind"));
            stringbuf_append_char(sb, '=');
            format_dot_value(sb, value, get_element_attribute(property, "value-source-kind"));
            first = false;
        }
        stringbuf_append_char(sb, ']');
    }
}

static void format_dot_properties(StringBuf* sb, const ElementReader& owner) {
    format_dot_properties_filtered(sb, owner, false);
}

static void format_dot_node_ref(StringBuf* sb, const ElementReader& value) {
    format_dot_value(sb, get_element_attribute(value, "id"),
                     get_element_attribute(value, "source-kind"));
    const char* port = get_element_attribute(value, "port");
    const char* compass = get_element_attribute(value, "compass");
    if (port) {
        stringbuf_append_char(sb, ':');
        format_dot_value(sb, port, nullptr);
    }
    if (compass) {
        stringbuf_append_char(sb, ':');
        format_dot_value(sb, compass, nullptr);
    }
}

static void format_dot_statements(StringBuf* sb, const ElementReader& owner,
                                  int depth, bool canonical);

static void format_dot_subgraph(StringBuf* sb, const ElementReader& group,
                                int depth, bool canonical, bool endpoint) {
    const char* source_kind = get_element_attribute(group, "source-kind");
    bool anonymous = source_kind && strcmp(source_kind, "anonymous") == 0;
    if (!anonymous) {
        stringbuf_append_str(sb, "subgraph ");
        format_dot_value(sb, get_element_attribute(group, "id"), source_kind);
        stringbuf_append_char(sb, ' ');
    }
    stringbuf_append_str(sb, "{\n");
    if (canonical) {
        auto children = group.childElements();
        ElementReader child;
        while (children.next(&child)) {
            if (!child.hasTag("properties")) continue;
            format_dot_indent(sb, depth + 1);
            stringbuf_append_str(sb, "graph");
            format_dot_properties_filtered(sb, group, true);
            stringbuf_append_str(sb, ";\n");
            break;
        }
    }
    format_dot_statements(sb, group, depth + 1, canonical);
    format_dot_indent(sb, depth);
    stringbuf_append_char(sb, '}');
    if (!endpoint) stringbuf_append_str(sb, ";\n");
}

static void format_dot_source_endpoint(StringBuf* sb, const ElementReader& endpoint,
                                       int depth) {
    const char* kind = get_element_attribute(endpoint, "kind");
    if (kind && strcmp(kind, "subgraph") == 0) {
        ElementReader group = endpoint.findChildElement("subgraph");
        if (group.isValid()) format_dot_subgraph(sb, group, depth, false, true);
    } else {
        format_dot_node_ref(sb, endpoint);
    }
}

static void format_dot_edge(StringBuf* sb, const ElementReader& edge,
                            int depth, bool source) {
    format_dot_indent(sb, depth);
    if (source) {
        auto endpoints = edge.childElements();
        ElementReader endpoint;
        bool first = true;
        while (endpoints.next(&endpoint)) {
            if (!endpoint.hasTag("dot-endpoint")) continue;
            if (!first) {
                const char* op = get_element_attribute(endpoint, "operator");
                stringbuf_append_format(sb, " %s ", op ? op : "->");
            }
            format_dot_source_endpoint(sb, endpoint, depth);
            first = false;
        }
    } else {
        format_dot_value(sb, get_element_attribute(edge, "from"), nullptr);
        const char* from_port = get_element_attribute(edge, "from-port");
        const char* from_compass = get_element_attribute(edge, "from-compass");
        if (from_port) { stringbuf_append_char(sb, ':'); format_dot_value(sb, from_port, nullptr); }
        if (from_compass) { stringbuf_append_char(sb, ':'); format_dot_value(sb, from_compass, nullptr); }
        stringbuf_append_str(sb, edge.get_bool_attr("directed", true) ? " -> " : " -- ");
        format_dot_value(sb, get_element_attribute(edge, "to"), nullptr);
        const char* to_port = get_element_attribute(edge, "to-port");
        const char* to_compass = get_element_attribute(edge, "to-compass");
        if (to_port) { stringbuf_append_char(sb, ':'); format_dot_value(sb, to_port, nullptr); }
        if (to_compass) { stringbuf_append_char(sb, ':'); format_dot_value(sb, to_compass, nullptr); }
    }
    format_dot_properties(sb, edge);
    stringbuf_append_str(sb, ";\n");
}

static bool format_dot_has_direct_node(const ElementReader& owner, const char* id) {
    if (!id) return false;
    auto children = owner.childElements();
    ElementReader child;
    while (children.next(&child)) {
        const char* child_id = child.hasTag("node") ? get_element_attribute(child, "id") : nullptr;
        if (child_id && strcmp(child_id, id) == 0) return true;
    }
    return false;
}

static void format_dot_statement(StringBuf* sb, const ElementReader& value,
                                 const ElementReader& owner, int depth, bool canonical) {
    const char* tag = value.tagName();
    if (!tag || strcmp(tag, "properties") == 0 || strcmp(tag, "constraints") == 0 ||
        strcmp(tag, "label") == 0 || strcmp(tag, "content") == 0 ||
        strcmp(tag, "diagnostics") == 0) return;
    if (strcmp(tag, "dot-attr-statement") == 0) {
        format_dot_indent(sb, depth);
        stringbuf_append_str(sb, get_element_attribute(value, "target-kind"));
        format_dot_properties(sb, value);
        stringbuf_append_str(sb, ";\n");
    } else if (strcmp(tag, "dot-assignment") == 0) {
        format_dot_indent(sb, depth);
        format_dot_value(sb, get_element_attribute(value, "name"),
                         get_element_attribute(value, "name-source-kind"));
        stringbuf_append_char(sb, '=');
        format_dot_value(sb, get_element_attribute(value, "value"),
                         get_element_attribute(value, "value-source-kind"));
        stringbuf_append_str(sb, ";\n");
    } else if (strcmp(tag, "node") == 0) {
        format_dot_indent(sb, depth);
        format_dot_node_ref(sb, value);
        format_dot_properties(sb, value);
        stringbuf_append_str(sb, ";\n");
    } else if (strcmp(tag, "edge") == 0 || strcmp(tag, "dot-edge-statement") == 0) {
        format_dot_edge(sb, value, depth, !canonical);
    } else if (strcmp(tag, "subgraph") == 0) {
        format_dot_indent(sb, depth);
        format_dot_subgraph(sb, value, depth, canonical, false);
    } else if (canonical && strcmp(tag, "member") == 0) {
        const char* id = get_element_attribute(value, "node");
        if (!format_dot_has_direct_node(owner, id)) {
            format_dot_indent(sb, depth);
            format_dot_value(sb, id, nullptr);
            stringbuf_append_str(sb, ";\n");
        }
    }
}

static void format_dot_statements(StringBuf* sb, const ElementReader& owner,
                                  int depth, bool canonical) {
    auto children = owner.childElements();
    ElementReader child;
    while (children.next(&child)) format_dot_statement(sb, child, owner, depth, canonical);
}

static void format_dot_document(StringBuf* sb, const ElementReader& graph, bool canonical) {
    if (graph.get_bool_attr("strict")) stringbuf_append_str(sb, "strict ");
    stringbuf_append_str(sb, graph.get_bool_attr("directed", true) ? "digraph" : "graph");
    const char* id = get_element_attribute(graph, "id");
    if (id) {
        stringbuf_append_char(sb, ' ');
        format_dot_value(sb, id, get_element_attribute(graph, "source-kind"));
    }
    stringbuf_append_str(sb, " {\n");
    if (canonical) {
        auto children = graph.childElements();
        ElementReader child;
        while (children.next(&child)) {
            if (!child.hasTag("properties")) continue;
            format_dot_indent(sb, 1);
            stringbuf_append_str(sb, "graph");
            format_dot_properties(sb, graph);
            stringbuf_append_str(sb, ";\n");
            break;
        }
    }
    // Source-stage DOT must follow statement children; flattening loses defaults and endpoint sets.
    format_dot_statements(sb, graph, 1, canonical);
    stringbuf_append_str(sb, "}\n");
}

// ============================================================================
// Syntax-driven formatters
// ============================================================================

static void format_graph_children(StringBuf* sb, const ElementReader& element, const char* flavor, const GraphSyntax* syn) {
    auto it = element.children();
    ItemReader child_item;
    while (it.next(&child_item)) {
        if (child_item.isElement()) {
            ElementReader child = child_item.asElement();
            const char* tag = child.tagName();
            if (!tag) continue;
            if      (strcmp(tag, "node")    == 0) format_graph_node(sb, child, flavor, syn);
            else if (strcmp(tag, "edge")    == 0) format_graph_edge(sb, child, flavor, syn);
            else if (strcmp(tag, "cluster") == 0) format_graph_cluster(sb, child, flavor, syn);
        }
    }
}

static void format_graph_node(StringBuf* sb, const ElementReader& node, const char* flavor, const GraphSyntax* syn) {
    const char* id    = get_element_attribute(node, "id");
    const char* label = get_element_attribute(node, "label");
    if (!id) return;

    if (syn->node_needs_indent) stringbuf_append_str(sb, syn->node_indent);
    format_graph_string(sb, id, flavor);
    if (label) {
        stringbuf_append_str(sb, syn->node_lbl_open);
        format_graph_string(sb, label, flavor);
        stringbuf_append_str(sb, syn->node_lbl_close);
    }

    // D2 style block for shape/fill/stroke
    if (strcmp(flavor, "d2") == 0) {
        const char* shape  = get_element_attribute(node, "shape");
        const char* fill   = get_element_attribute(node, "fill");
        const char* stroke = get_element_attribute(node, "stroke");
        if (shape || fill || stroke) {
            stringbuf_append_str(sb, ": {\n");
            if (shape)  { stringbuf_append_str(sb, "  shape: ");  stringbuf_append_str(sb, shape);  stringbuf_append_char(sb, '\n'); }
            if (fill || stroke) {
                stringbuf_append_str(sb, "  style: {\n");
                if (fill)   { stringbuf_append_str(sb, "    fill: ");   stringbuf_append_str(sb, fill);   stringbuf_append_char(sb, '\n'); }
                if (stroke) { stringbuf_append_str(sb, "    stroke: "); stringbuf_append_str(sb, stroke); stringbuf_append_char(sb, '\n'); }
                stringbuf_append_str(sb, "  }\n");
            }
            stringbuf_append_str(sb, "}\n");
            return;
        }
    }
    stringbuf_append_str(sb, syn->stmt_end);
}

static void format_graph_edge(StringBuf* sb, const ElementReader& edge, const char* flavor, const GraphSyntax* syn) {
    const char* from  = get_element_attribute(edge, "from");
    const char* to    = get_element_attribute(edge, "to");
    const char* label = get_element_attribute(edge, "label");
    if (!from || !to) return;

    if (syn->node_needs_indent) stringbuf_append_str(sb, syn->node_indent);
    format_graph_string(sb, from, flavor);
    stringbuf_append_str(sb, syn->edge_arrow);
    format_graph_string(sb, to, flavor);
    if (label) {
        stringbuf_append_str(sb, syn->edge_label_sep);
        format_graph_string(sb, label, flavor);
        stringbuf_append_str(sb, syn->edge_label_close);
    }
    stringbuf_append_str(sb, syn->stmt_end);
}

static void format_graph_cluster(StringBuf* sb, const ElementReader& cluster, const char* flavor, const GraphSyntax* syn) {
    const char* id    = get_element_attribute(cluster, "id");
    const char* label = get_element_attribute(cluster, "label");
    const char* eff_id = id ? id : syn->cluster_id_default;

    stringbuf_append_str(sb, syn->cluster_open);
    format_graph_string(sb, eff_id, flavor);

    if (syn->cluster_label_inline) {
        // mermaid: label is appended right after id on same line
        if (label) {
            stringbuf_append_str(sb, syn->cluster_label_pfx);
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, syn->cluster_label_sfx);
        } else {
            stringbuf_append_char(sb, '\n');
        }
    } else {
        stringbuf_append_str(sb, syn->cluster_body_open);
        if (label) {
            stringbuf_append_str(sb, syn->cluster_label_pfx);
            format_graph_string(sb, label, flavor);
            stringbuf_append_str(sb, syn->cluster_label_sfx);
        }
    }

    format_graph_children(sb, cluster, flavor, syn);
    stringbuf_append_str(sb, syn->cluster_body_close);
}

static void format_graph_element_with_syn(StringBuf* sb, const ElementReader& element, const char* flavor, const GraphSyntax* syn) {
    const char* tag = element.tagName();
    if (!tag || strcmp(tag, "graph") != 0) {
        if (tag) log_debug("graph: expected graph element, got %s", tag);
        return;
    }

    const char* graph_type   = get_element_attribute(element, "type");
    const char* graph_layout = get_element_attribute(element, "layout");
    const char* graph_name   = get_element_attribute(element, "name");

    log_debug("graph: formatting as %s (type: %s)", flavor, graph_type ? graph_type : "unknown");

    if (strcmp(flavor, "dot") == 0) {
        const char* stage = get_element_attribute(element, "ir-stage");
        if (stage && (strcmp(stage, "source") == 0 || strcmp(stage, "canonical") == 0)) {
            format_dot_document(sb, element, strcmp(stage, "canonical") == 0);
            return;
        }
        bool directed = graph_type && strcmp(graph_type, "directed") == 0;
        stringbuf_append_str(sb, directed ? "digraph " : "graph ");
        format_graph_string(sb, graph_name ? graph_name : "G", flavor);
        stringbuf_append_str(sb, " {\n");
        if (graph_layout) {
            stringbuf_append_str(sb, "    layout=");
            format_graph_string(sb, graph_layout, flavor);
            stringbuf_append_str(sb, ";\n");
        }
        format_graph_children(sb, element, flavor, syn);
        stringbuf_append_str(sb, "}\n");

    } else if (strcmp(flavor, "mermaid") == 0) {
        bool directed = graph_type && strcmp(graph_type, "directed") == 0;
        stringbuf_append_str(sb, directed ? "flowchart TD\n" : "graph LR\n");
        format_graph_children(sb, element, flavor, syn);

    } else if (strcmp(flavor, "d2") == 0) {
        if (graph_name || graph_type) {
            stringbuf_append_str(sb, "# Graph: ");
            if (graph_name)  stringbuf_append_str(sb, graph_name);
            if (graph_type) {
                stringbuf_append_str(sb, " (");
                stringbuf_append_str(sb, graph_type);
                stringbuf_append_char(sb, ')');
            }
            stringbuf_append_char(sb, '\n');
        }
        format_graph_children(sb, element, flavor, syn);

    } else {
        log_error("graph: unsupported flavor: %s", flavor);
    }
}

// ============================================================================
// Public entry points
// ============================================================================

String* format_graph(Pool* pool, Item root_item) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) { log_error("format_graph: not an element"); return NULL; }
    StringBuf* sb = stringbuf_new(pool);
    ItemReader root(root_item.to_const());
    ElementReader elem = root.asElement();
    const GraphSyntax* syn = get_graph_syntax("dot");
    format_graph_element_with_syn(sb, elem, "dot", syn);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}

String* format_graph_with_flavor(Pool* pool, Item root_item, const char* flavor) {
    if (get_type_id(root_item) != LMD_TYPE_ELEMENT) { log_error("format_graph_with_flavor: not an element"); return NULL; }
    StringBuf* sb = stringbuf_new(pool);
    const char* eff_flavor = flavor ? flavor : "dot";
    const GraphSyntax* syn = get_graph_syntax(eff_flavor);
    ItemReader root(root_item.to_const());
    ElementReader elem = root.asElement();
    format_graph_element_with_syn(sb, elem, eff_flavor, syn);
    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}
