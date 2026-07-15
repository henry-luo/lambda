#include "input-graph.h"
#include "../mark_reader.hpp"
#include <string.h>
#include "lib/log.h"

using namespace lambda;

void graph_set_source_span(InputContext& ctx, Element* element,
                           const SourceLocation& start, const SourceLocation& end,
                           bool preserve_existing) {
    if (!element) return;
    if (preserve_existing && !ElementReader(element).get_attr("source-start").isNull()) return;
    add_graph_integer_attribute(ctx.input(), element, "source-start", (int64_t)start.offset);
    add_graph_integer_attribute(ctx.input(), element, "source-end", (int64_t)end.offset);
    add_graph_integer_attribute(ctx.input(), element, "source-line", (int64_t)start.line);
    add_graph_integer_attribute(ctx.input(), element, "source-column", (int64_t)start.column);
}

static const char* graph_diagnostic_severity(ParseErrorSeverity severity) {
    if (severity == ParseErrorSeverity::WARNING) return "warning";
    if (severity == ParseErrorSeverity::NOTE) return "note";
    return "error";
}

void graph_append_diagnostics(InputContext& ctx, Element* graph, const char* default_code) {
    const ParseErrorList& errors = ctx.errors();
    if (errors.size() == 0) return;
    Element* diagnostics = ctx.builder.element("diagnostics").final().element;
    for (size_t index = 0; index < errors.size(); index++) {
        ParseError* error = errors.getError(index);
        if (!error) continue;
        ElementBuilder diagnostic = ctx.builder.element("diagnostic");
        diagnostic.attr("code", error->code ? error->code : default_code)
            .attr("severity", graph_diagnostic_severity(error->severity))
            .attr("message", error->message ? error->message : "Graph parse error")
            .attr("source-start", (int64_t)error->location.offset)
            .attr("source-line", (int64_t)error->location.line)
            .attr("source-column", (int64_t)error->location.column);
        if (error->context_line) diagnostic.attr("context", error->context_line);
        if (error->hint) diagnostic.attr("hint", error->hint);
        add_node_to_graph(ctx.input(), diagnostics, diagnostic.final().element);
    }
    add_node_to_graph(ctx.input(), graph, diagnostics);
}

// Main graph parser function that dispatches to specific flavors
void parse_graph(Input* input, const char* graph_string, const char* flavor) {
    if (!flavor) {
        flavor = "dot"; // Default to DOT format
    }

    if (strcmp(flavor, "dot") == 0 || strcmp(flavor, "graphviz") == 0) {
        parse_graph_dot(input, graph_string);
    } else if (strcmp(flavor, "mermaid") == 0) {
        parse_graph_mermaid(input, graph_string);
    } else if (strcmp(flavor, "d2") == 0) {
        parse_graph_d2(input, graph_string);
    } else {
        log_debug("Unknown graph flavor: %s\n", flavor);
        // Default to DOT parser
        parse_graph_dot(input, graph_string);
    }
}

// Helper function to create a graph element
Element* create_graph_element(Input* input, const char* type, const char* layout, const char* flavor) {
    MarkBuilder builder(input);
    ElementBuilder graph = builder.element("graph");

    // Add basic graph attributes with CSS-aligned naming
    graph.attr("type", type);
    graph.attr("layout", layout);
    graph.attr("flavor", flavor);

    // Note: No separate arrays for nodes/edges/clusters - they are direct children
    // The Lambda Element system automatically manages child elements

    // Return raw Element* for compatibility with existing code
    return graph.final().element;
}

// Helper function to create a node element
// shape parameter is optional - if provided, it's added before finalization
Element* create_node_element(Input* input, const char* id, const char* label, const char* shape) {
    MarkBuilder builder(input);
    ElementBuilder node = builder.element("node");

    node.attr("id", id);
    if (label) {
        node.attr("label", label);
    }
    if (shape && shape[0] != '\0') {
        node.attr("shape", shape);
    }

    // Note: Attributes are now stored directly in the element, no separate attributes map

    // Return raw Element* for compatibility with existing code
    return node.final().element;
}

// Helper function to create an edge element
// style, arrow_start, arrow_end are optional - if provided, they're added before finalization
Element* create_edge_element(Input* input, const char* from, const char* to, const char* label,
                             const char* style, const char* arrow_start, const char* arrow_end) {
    MarkBuilder builder(input);
    ElementBuilder edge = builder.element("edge");

    edge.attr("from", from);
    edge.attr("to", to);
    if (label) {
        edge.attr("label", label);
    }
    if (style && style[0] != '\0') {
        edge.attr("style", style);
    }
    if (arrow_start && arrow_start[0] != '\0') {
        edge.attr("arrow-start", arrow_start);
    }
    if (arrow_end && arrow_end[0] != '\0') {
        edge.attr("arrow-end", arrow_end);
    }

    // Note: Attributes are now stored directly in the element, no separate attributes map

    // Return raw Element* for compatibility with existing code
    return edge.final().element;
}

// Helper function to create a cluster element
Element* create_cluster_element(Input* input, const char* id, const char* label) {
    MarkBuilder builder(input);
    ElementBuilder cluster = builder.element("subgraph");

    cluster.attr("id", id);
    if (label) {
        cluster.attr("label", label);
    }

    // Note: Subgraphs contain direct child nodes and edges, no separate arrays

    // Return raw Element* for compatibility with existing code
    return cluster.final().element;
}

// Helper function to add an attribute to any graph element
// Generic function to add graph attributes with CSS-aligned naming
void add_graph_attribute(Input* input, Element* element, const char* name, const char* value) {
    if (!element || !name || !value) return;

    MarkBuilder builder(input);

    // Convert legacy attribute names to CSS-aligned equivalents
    const char* css_name = name;

    // CSS-aligned attribute name mapping
    if (strcmp(name, "fontsize") == 0) {
        css_name = "font-size";
    } else if (strcmp(name, "fontcolor") == 0) {
        css_name = "color";
    } else if (strcmp(name, "fontname") == 0 || strcmp(name, "font") == 0) {
        css_name = "font-family";
    } else if (strcmp(name, "arrowhead") == 0) {
        css_name = "arrow-head";
    } else if (strcmp(name, "arrowtail") == 0) {
        css_name = "arrow-tail";
    } else if (strcmp(name, "labelpos") == 0) {
        css_name = "label-position";
    } else if (strcmp(name, "rankdir") == 0) {
        css_name = "rank-dir";
    } else if (strcmp(name, "width") == 0 &&
               (((TypeElmt*)element->type)->name.str &&
                strcmp(((TypeElmt*)element->type)->name.str, "edge") == 0)) {
        // For edges, width becomes stroke-width
        css_name = "stroke-width";
    } else if (strcmp(name, "style") == 0 &&
               (((TypeElmt*)element->type)->name.str &&
                strcmp(((TypeElmt*)element->type)->name.str, "edge") == 0)) {
        // For edges, style becomes stroke-dasharray
        css_name = "stroke-dasharray";
    }

    // Add the attribute directly to the element using putToElement
    String* key = builder.createString(css_name);
    String* val = builder.createString(value);
    if (key && val) {
        Item lambda_value = {.item = s2it(val)};
        builder.putToElement(lam::gc_borrow(element), key, lambda_value);
    }
}

void add_graph_integer_attribute(Input* input, Element* element, const char* name, int64_t value) {
    if (!element || !name) return;
    MarkBuilder builder(input);
    String* key = builder.createString(name);
    if (key) {
        builder.putToElement(lam::gc_borrow(element), key, builder.createInt(value));
    }
}

static void add_child_to_graph_element(Input* input, Element* graph, Element* child) {
    if (!graph || !child) return;

    array_append((Array*)graph, (Item){.element = child}, input->pool, input->arena);
    TypeElmt* graph_type = graph->type ? (TypeElmt*)graph->type : NULL;
    if (graph_type && graph_type->type_id == LMD_TYPE_ELEMENT) {
        // Graph parsers append children after ElementBuilder::final(); keep the
        // finalized type's content count aligned with the list length.
        graph_type->content_length = ((List*)graph)->length;
    }
}

// Helper function to add a node to a graph (as direct child)
void add_node_to_graph(Input* input, Element* graph, Element* node) {
    add_child_to_graph_element(input, graph, node);
}

// Helper function to add an edge to a graph (as direct child)
void add_edge_to_graph(Input* input, Element* graph, Element* edge) {
    add_child_to_graph_element(input, graph, edge);
}

// Helper function to add a cluster to a graph (as direct child)
void add_cluster_to_graph(Input* input, Element* graph, Element* cluster) {
    add_child_to_graph_element(input, graph, cluster);
}

// Helper functions for attribute parsing with CSS-aligned naming
void add_node_attributes(Input* input, Element* node, const char* attr_string) {
    // Parse attribute string and add individual attributes directly to node
    // This would parse strings like: [shape=circle, fill=red, font-size=12]
    // For now, this is a placeholder for the full parsing implementation
    if (!attr_string || !node) return;

    // TODO: Implement full attribute string parsing
    // Should handle CSS-aligned attribute names
}

void add_edge_attributes(Input* input, Element* edge, const char* attr_string) {
    // Parse attribute string and add individual attributes directly to edge
    // This would parse strings like: [color=blue, stroke-width=2, stroke-dasharray=dashed]
    // For now, this is a placeholder for the full parsing implementation
    if (!attr_string || !edge) return;

    // TODO: Implement full attribute string parsing
    // Should handle CSS-aligned attribute names
}
