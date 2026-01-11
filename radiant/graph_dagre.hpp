#ifndef RADIANT_GRAPH_DAGRE_HPP
#define RADIANT_GRAPH_DAGRE_HPP

#include "graph_layout_types.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Main Dagre layout algorithm
void layout_graph_dagre(LayoutGraph* graph, GraphLayoutOptions* opts);

// Algorithm phases (can be called individually for testing)
void dagre_assign_ranks(LayoutGraph* graph);
void dagre_create_layers(LayoutGraph* graph);
void dagre_reduce_crossings(LayoutGraph* graph, int max_iterations);
void dagre_assign_coordinates(LayoutGraph* graph, GraphLayoutOptions* opts);
void dagre_route_edges(LayoutGraph* graph, bool use_splines);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_GRAPH_DAGRE_HPP
