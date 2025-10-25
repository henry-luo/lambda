#include "input-graph.h"
#include <string.h>

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
        printf("Unknown graph flavor: %s\n", flavor);
        // Default to DOT parser
        parse_graph_dot(input, graph_string);
    }
}

// Helper function to create a graph element
Element* create_graph_element(Input* input, const char* type, const char* layout, const char* flavor) {
    Element* graph = input_create_element(input, "graph");
    if (!graph) return NULL;
    
    // Add basic graph attributes with CSS-aligned naming
    input_add_attribute_to_element(input, graph, "type", type);
    input_add_attribute_to_element(input, graph, "layout", layout);
    input_add_attribute_to_element(input, graph, "flavor", flavor);
    
    // Note: No separate arrays for nodes/edges/clusters - they are direct children
    // The Lambda Element system automatically manages child elements
    
    return graph;
}

// Helper function to create a node element
Element* create_node_element(Input* input, const char* id, const char* label) {
    Element* node = input_create_element(input, "node");
    if (!node) return NULL;
    
    input_add_attribute_to_element(input, node, "id", id);
    if (label) {
        input_add_attribute_to_element(input, node, "label", label);
    }
    
    // Note: Attributes are now stored directly in the element, no separate attributes map
    
    return node;
}

// Helper function to create an edge element
Element* create_edge_element(Input* input, const char* from, const char* to, const char* label) {
    Element* edge = input_create_element(input, "edge");
    if (!edge) return NULL;
    
    input_add_attribute_to_element(input, edge, "from", from);
    input_add_attribute_to_element(input, edge, "to", to);
    if (label) {
        input_add_attribute_to_element(input, edge, "label", label);
    }
    
    // Note: Attributes are now stored directly in the element, no separate attributes map
    
    return edge;
}

// Helper function to create a cluster element
Element* create_cluster_element(Input* input, const char* id, const char* label) {
    Element* cluster = input_create_element(input, "subgraph");
    if (!cluster) return NULL;
    
    input_add_attribute_to_element(input, cluster, "id", id);
    if (label) {
        input_add_attribute_to_element(input, cluster, "label", label);
    }
    
    // Note: Subgraphs contain direct child nodes and edges, no separate arrays
    
    return cluster;
}

// Helper function to add an attribute to any graph element
// Generic function to add graph attributes with CSS-aligned naming
void add_graph_attribute(Input* input, Element* element, const char* name, const char* value) {
    if (!element || !name || !value) return;
    
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
    
    // Add the attribute directly to the element
    input_add_attribute_to_element(input, element, css_name, value);
}

// Helper function to add a node to a graph (as direct child)
void add_node_to_graph(Input* input, Element* graph, Element* node) {
    if (!graph || !node) return;
    
    // Add node as direct child element to the graph
    // In the Lambda Element system, this would be managed automatically
    // when the node is parsed within the graph context
    
    // For now, we'll use the existing child management system
    // This is a simplified implementation that relies on the Lambda Element list behavior
    
    // The Element inherits from List, so we can add child items
    if (graph->items == NULL) {
        // Initialize the items array if not already done
        graph->capacity = 10;
        graph->items = (Item*)pool_alloc(input->pool, sizeof(Item) * graph->capacity);
        graph->length = 0;
    }
    
    if (graph->length < graph->capacity) {
        Item node_item;
        node_item.container = (Container*)node;
        node_item.type_id = LMD_TYPE_ELEMENT;
        graph->items[graph->length++] = node_item;
    }
}

// Helper function to add an edge to a graph (as direct child)
void add_edge_to_graph(Input* input, Element* graph, Element* edge) {
    if (!graph || !edge) return;
    
    // Add edge as direct child element to the graph
    // Similar to add_node_to_graph implementation
    
    if (graph->items == NULL) {
        // Initialize the items array if not already done
        graph->capacity = 10;
        graph->items = (Item*)pool_alloc(input->pool, sizeof(Item) * graph->capacity);
        graph->length = 0;
    }
    
    if (graph->length < graph->capacity) {
        Item edge_item;
        edge_item.container = (Container*)edge;
        edge_item.type_id = LMD_TYPE_ELEMENT;
        graph->items[graph->length++] = edge_item;
    }
}

// Helper function to add a cluster to a graph (as direct child)
void add_cluster_to_graph(Input* input, Element* graph, Element* cluster) {
    if (!graph || !cluster) return;
    
    // Add cluster as direct child element to the graph
    // Similar to add_node_to_graph implementation
    
    if (graph->items == NULL) {
        // Initialize the items array if not already done
        graph->capacity = 10;
        graph->items = (Item*)pool_alloc(input->pool, sizeof(Item) * graph->capacity);
        graph->length = 0;
    }
    
    if (graph->length < graph->capacity) {
        Item cluster_item;
        cluster_item.container = (Container*)cluster;
        cluster_item.type_id = LMD_TYPE_ELEMENT;
        graph->items[graph->length++] = cluster_item;
    }
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