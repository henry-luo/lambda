#include "format.h"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"

// ============================================================================
// Escape rules (Problem 1 — already table-driven)
// ============================================================================

struct GraphEscapeRules {
    const char* trigger_chars;  // chars that require quoting
    bool escape_backslash;
};

static const GraphEscapeRules GRAPH_ESCAPE_DOT     = {" ->{}\"",  true};
static const GraphEscapeRules GRAPH_ESCAPE_MERMAID = {" -",       false};
static const GraphEscapeRules GRAPH_ESCAPE_D2      = {" :{}\"->", false};

static void format_graph_string(StringBuf* sb, const char* str, const char* flavor) {
    if (!str) return;
    const GraphEscapeRules* rules =
        (flavor && strcmp(flavor, "mermaid") == 0) ? &GRAPH_ESCAPE_MERMAID :
        (flavor && strcmp(flavor, "d2")      == 0) ? &GRAPH_ESCAPE_D2      :
                                                      &GRAPH_ESCAPE_DOT;
    bool needs_quotes = (strpbrk(str, rules->trigger_chars) != NULL);
    if (needs_quotes) stringbuf_append_char(sb, '"');
    for (const char* p = str; *p; p++) {
        if (*p == '"')                             { stringbuf_append_str(sb, "\\\""); }
        else if (*p == '\\' && rules->escape_backslash) { stringbuf_append_str(sb, "\\\\"); }
        else                                       { stringbuf_append_char(sb, *p); }
    }
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
