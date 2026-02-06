#ifndef LAMBDA_INPUT_GRAPH_H
#define LAMBDA_INPUT_GRAPH_H

#include "input.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Graph parsing functions
void parse_graph(Input* input, const char* graph_string, const char* flavor);
void parse_graph_dot(Input* input, const char* dot_string);
void parse_graph_mermaid(Input* input, const char* mermaid_string);
void parse_graph_d2(Input* input, const char* d2_string);

// Helper functions for graph construction
Element* create_graph_element(Input* input, const char* type, const char* layout, const char* flavor);
Element* create_node_element(Input* input, const char* id, const char* label, const char* shape = nullptr);
Element* create_edge_element(Input* input, const char* from, const char* to, const char* label,
                             const char* style = nullptr, const char* arrow_start = nullptr,
                             const char* arrow_end = nullptr);
Element* create_cluster_element(Input* input, const char* id, const char* label);

// Attribute management
void add_graph_attribute(Input* input, Element* element, const char* name, const char* value);
void add_node_attributes(Input* input, Element* node, const char* attr_string);
void add_edge_attributes(Input* input, Element* edge, const char* attr_string);

// Graph structure manipulation
void add_node_to_graph(Input* input, Element* graph, Element* node);
void add_edge_to_graph(Input* input, Element* graph, Element* edge);
void add_cluster_to_graph(Input* input, Element* graph, Element* cluster);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_INPUT_GRAPH_H
