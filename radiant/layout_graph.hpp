#ifndef RADIANT_LAYOUT_GRAPH_HPP
#define RADIANT_LAYOUT_GRAPH_HPP

#include "graph_layout_types.hpp"
#include "../lambda/lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Main layout function - auto-detects algorithm from graph attributes
GraphLayout* layout_graph(Element* graph);

// Layout with explicit algorithm
GraphLayout* layout_graph_with_algorithm(Element* graph, const char* algorithm);

// Layout with full options
GraphLayout* layout_graph_with_options(Element* graph, GraphLayoutOptions* opts);

// Free layout resources
void free_graph_layout(GraphLayout* layout);

// Get default options
GraphLayoutOptions* create_default_layout_options();

#ifdef __cplusplus
}
#endif

#endif // RADIANT_LAYOUT_GRAPH_HPP
