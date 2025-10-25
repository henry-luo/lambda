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
    
    // Add basic graph attributes
    input_add_attribute_to_element(input, graph, "type", type);
    input_add_attribute_to_element(input, graph, "layout", layout);
    input_add_attribute_to_element(input, graph, "flavor", flavor);
    
    // Create empty arrays for nodes, edges, and clusters
    Array* nodes = array_pooled(input->pool);
    Array* edges = array_pooled(input->pool);
    Array* clusters = array_pooled(input->pool);
    
    if (nodes && edges && clusters) {
        Item nodes_item;
        nodes_item.container = (Container*)nodes;
        nodes_item.type_id = LMD_TYPE_ARRAY;
        
        Item edges_item;
        edges_item.container = (Container*)edges;
        edges_item.type_id = LMD_TYPE_ARRAY;
        
        Item clusters_item;
        clusters_item.container = (Container*)clusters;
        clusters_item.type_id = LMD_TYPE_ARRAY;
        
        input_add_attribute_item_to_element(input, graph, "nodes", nodes_item);
        input_add_attribute_item_to_element(input, graph, "edges", edges_item);
        input_add_attribute_item_to_element(input, graph, "clusters", clusters_item);
    }
    
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
    
    // Create empty attributes map
    Map* attributes = map_pooled(input->pool);
    if (attributes) {
        Item attr_item;
        attr_item.container = (Container*)attributes;
        attr_item.type_id = LMD_TYPE_MAP;
        input_add_attribute_item_to_element(input, node, "attributes", attr_item);
    }
    
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
    
    // Create empty attributes map
    Map* attributes = map_pooled(input->pool);
    if (attributes) {
        Item attr_item;
        attr_item.container = (Container*)attributes;
        attr_item.type_id = LMD_TYPE_MAP;
        input_add_attribute_item_to_element(input, edge, "attributes", attr_item);
    }
    
    return edge;
}

// Helper function to create a cluster element
Element* create_cluster_element(Input* input, const char* id, const char* label) {
    Element* cluster = input_create_element(input, "cluster");
    if (!cluster) return NULL;
    
    input_add_attribute_to_element(input, cluster, "id", id);
    if (label) {
        input_add_attribute_to_element(input, cluster, "label", label);
    }
    
    // Create empty arrays for nodes, edges, and clusters
    Array* nodes = array_pooled(input->pool);
    Array* edges = array_pooled(input->pool);
    Array* clusters = array_pooled(input->pool);
    Map* attributes = map_pooled(input->pool);
    
    if (nodes && edges && clusters && attributes) {
        Item nodes_item;
        nodes_item.container = (Container*)nodes;
        nodes_item.type_id = LMD_TYPE_ARRAY;
        
        Item edges_item;
        edges_item.container = (Container*)edges;
        edges_item.type_id = LMD_TYPE_ARRAY;
        
        Item clusters_item;
        clusters_item.container = (Container*)clusters;
        clusters_item.type_id = LMD_TYPE_ARRAY;
        
        Item attr_item;
        attr_item.container = (Container*)attributes;
        attr_item.type_id = LMD_TYPE_MAP;
        
        input_add_attribute_item_to_element(input, cluster, "nodes", nodes_item);
        input_add_attribute_item_to_element(input, cluster, "edges", edges_item);
        input_add_attribute_item_to_element(input, cluster, "clusters", clusters_item);
        input_add_attribute_item_to_element(input, cluster, "attributes", attr_item);
    }
    
    return cluster;
}

// Helper function to add an attribute to any graph element
void add_graph_attribute(Input* input, Element* element, const char* name, const char* value) {
    if (!element || !name || !value) return;
    
    // First try to add as direct attribute
    input_add_attribute_to_element(input, element, name, value);
    
    // Also add to the attributes map if it exists
    // This allows both direct access and iteration over all attributes
    // Note: We would need to implement a helper to get the attributes map from the element
    // For now, just add as direct attribute
}

// Helper function to add a node to a graph
void add_node_to_graph(Input* input, Element* graph, Element* node) {
    if (!graph || !node) return;
    
    // Find the nodes array in the graph
    // This is a simplified implementation - in practice, we'd need to properly
    // access the element's attributes to get the nodes array
    // For now, we'll add the node as content
    
    // Add node as element content - this is a placeholder
    // The actual implementation would need to properly manage the nodes array
}

// Helper function to add an edge to a graph
void add_edge_to_graph(Input* input, Element* graph, Element* edge) {
    if (!graph || !edge) return;
    
    // Similar to add_node_to_graph, this is a placeholder
    // The actual implementation would properly manage the edges array
}

// Helper function to add a cluster to a graph
void add_cluster_to_graph(Input* input, Element* graph, Element* cluster) {
    if (!graph || !cluster) return;
    
    // Similar to add_node_to_graph, this is a placeholder
    // The actual implementation would properly manage the clusters array
}

// Helper functions for attribute parsing (used by DOT parser)
void add_node_attributes(Input* input, Element* node, const char* attr_string) {
    // Parse attribute string and add to node
    // This is a placeholder - full implementation would parse the string
    // and add individual attributes
}

void add_edge_attributes(Input* input, Element* edge, const char* attr_string) {
    // Parse attribute string and add to edge
    // This is a placeholder - full implementation would parse the string
    // and add individual attributes
}