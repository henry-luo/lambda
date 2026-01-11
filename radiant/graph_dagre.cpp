#include "graph_dagre.hpp"
#include "../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

// ============================================================================
// Phase 1: Rank Assignment (Longest Path Algorithm)
// ============================================================================

// compute longest path from roots using DFS
static void compute_rank_dfs(LayoutNode* node, LayoutGraph* graph, int* visited_flags) {
    // find node index
    int node_idx = -1;
    for (int i = 0; i < graph->nodes->length; i++) {
        if ((LayoutNode*)graph->nodes->data[i] == node) {
            node_idx = i;
            break;
        }
    }
    
    if (node_idx < 0 || visited_flags[node_idx]) {
        return;
    }
    visited_flags[node_idx] = 1;
    
    int max_predecessor_rank = -1;
    
    // visit all predecessors first
    for (int i = 0; i < node->in_edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)node->in_edges->data[i];
        compute_rank_dfs(edge->from_node, graph, visited_flags);
        
        int pred_rank = edge->from_node->rank;
        if (pred_rank > max_predecessor_rank) {
            max_predecessor_rank = pred_rank;
        }
    }
    
    int rank = max_predecessor_rank + 1;
    node->rank = rank;
}

void dagre_assign_ranks(LayoutGraph* graph) {
    log_debug("dagre: assigning ranks (longest path algorithm)");
    
    int* visited_flags = (int*)calloc(graph->nodes->length, sizeof(int));
    
    // initialize all ranks to 0
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        node->rank = 0;
    }
    
    // find root nodes (no incoming edges) and start DFS
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        if (node->in_edges->length == 0) {
            compute_rank_dfs(node, graph, visited_flags);
        }
    }
    
    // handle nodes not reachable from roots (disconnected components or cycles)
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        if (!visited_flags[i]) {
            // assign rank based on longest path from any predecessor
            compute_rank_dfs(node, graph, visited_flags);
        }
    }
    
    free(visited_flags);
    
    log_debug("dagre: rank assignment complete");
}

// ============================================================================
// Phase 2: Create Layers from Ranks
// ============================================================================

void dagre_create_layers(LayoutGraph* graph) {
    log_debug("dagre: creating layers from ranks");
    
    // find max rank
    int max_rank = 0;
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        if (node->rank > max_rank) {
            max_rank = node->rank;
        }
    }
    
    // create layers
    for (int r = 0; r <= max_rank; r++) {
        LayoutLayer* layer = (LayoutLayer*)calloc(1, sizeof(LayoutLayer));
        layer->rank = r;
        layer->nodes = arraylist_new(8);
        arraylist_append(graph->layers, layer);
    }
    
    // assign nodes to layers
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        LayoutLayer* layer = (LayoutLayer*)graph->layers->data[node->rank];
        arraylist_append(layer->nodes, node);
        node->order = layer->nodes->length - 1;
    }
    
    log_debug("dagre: created %d layers", max_rank + 1);
}

// ============================================================================
// Phase 3: Crossing Reduction (Barycenter Heuristic)
// ============================================================================

// compute barycenter (average position of neighbors)
static float compute_barycenter(LayoutNode* node, bool use_predecessors) {
    ArrayList* edges = use_predecessors ? node->in_edges : node->out_edges;
    
    if (edges->length == 0) {
        return (float)node->order;
    }
    
    float sum = 0.0f;
    for (int i = 0; i < edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)edges->data[i];
        LayoutNode* neighbor = use_predecessors ? edge->from_node : edge->to_node;
        sum += (float)neighbor->order;
    }
    
    return sum / (float)edges->length;
}

// count crossings between two adjacent layers
static int count_crossings_between_layers(LayoutLayer* layer1, LayoutLayer* layer2) {
    int crossings = 0;
    
    for (int i = 0; i < layer1->nodes->length; i++) {
        LayoutNode* node_i = (LayoutNode*)layer1->nodes->data[i];
        
        for (int j = i + 1; j < layer1->nodes->length; j++) {
            LayoutNode* node_j = (LayoutNode*)layer1->nodes->data[j];
            
            // check all edge pairs between these nodes and layer2
            for (int ei = 0; ei < node_i->out_edges->length; ei++) {
                LayoutEdge* edge_i = (LayoutEdge*)node_i->out_edges->data[ei];
                if (edge_i->to_node->rank != layer2->rank) continue;
                
                for (int ej = 0; ej < node_j->out_edges->length; ej++) {
                    LayoutEdge* edge_j = (LayoutEdge*)node_j->out_edges->data[ej];
                    if (edge_j->to_node->rank != layer2->rank) continue;
                    
                    // check if edges cross
                    if (edge_i->to_node->order > edge_j->to_node->order) {
                        crossings++;
                    }
                }
            }
        }
    }
    
    return crossings;
}

// comparison function for sorting nodes by barycenter
typedef struct {
    LayoutNode* node;
    float barycenter;
} NodeWithBarycenter;

static int compare_by_barycenter(const void* a, const void* b) {
    float bc_a = ((NodeWithBarycenter*)a)->barycenter;
    float bc_b = ((NodeWithBarycenter*)b)->barycenter;
    
    if (bc_a < bc_b) return -1;
    if (bc_a > bc_b) return 1;
    return 0;
}

void dagre_reduce_crossings(LayoutGraph* graph, int max_iterations) {
    log_debug("dagre: reducing crossings (barycenter heuristic)");
    
    if (graph->layers->length < 2) {
        return; // no crossings possible
    }
    
    int initial_crossings = 0;
    for (int i = 0; i < graph->layers->length - 1; i++) {
        LayoutLayer* layer1 = (LayoutLayer*)graph->layers->data[i];
        LayoutLayer* layer2 = (LayoutLayer*)graph->layers->data[i + 1];
        initial_crossings += count_crossings_between_layers(layer1, layer2);
    }
    
    log_debug("initial crossings: %d", initial_crossings);
    
    int best_crossings = initial_crossings;
    int iteration = 0;
    
    while (iteration < max_iterations) {
        bool improved = false;
        
        // sweep down: order each layer by barycenter of predecessors
        for (int i = 1; i < graph->layers->length; i++) {
            LayoutLayer* layer = (LayoutLayer*)graph->layers->data[i];
            
            int node_count = layer->nodes->length;
            if (node_count == 0) continue;
            
            // compute barycenters
            NodeWithBarycenter* nodes_with_bc = (NodeWithBarycenter*)malloc(
                node_count * sizeof(NodeWithBarycenter));
            
            for (int j = 0; j < node_count; j++) {
                LayoutNode* node = (LayoutNode*)layer->nodes->data[j];
                nodes_with_bc[j].node = node;
                nodes_with_bc[j].barycenter = compute_barycenter(node, true);
            }
            
            // sort by barycenter
            qsort(nodes_with_bc, node_count, sizeof(NodeWithBarycenter),
                  compare_by_barycenter);
            
            // update layer and node orders
            arraylist_clear(layer->nodes);
            for (int j = 0; j < node_count; j++) {
                LayoutNode* node = nodes_with_bc[j].node;
                node->order = j;
                arraylist_append(layer->nodes, node);
            }
            
            free(nodes_with_bc);
        }
        
        // count crossings after down sweep
        int crossings = 0;
        for (int i = 0; i < graph->layers->length - 1; i++) {
            LayoutLayer* layer1 = (LayoutLayer*)graph->layers->data[i];
            LayoutLayer* layer2 = (LayoutLayer*)graph->layers->data[i + 1];
            crossings += count_crossings_between_layers(layer1, layer2);
        }
        
        if (crossings < best_crossings) {
            best_crossings = crossings;
            improved = true;
        }
        
        // sweep up: order each layer by barycenter of successors
        for (int i = (int)graph->layers->length - 2; i >= 0; i--) {
            LayoutLayer* layer = (LayoutLayer*)graph->layers->data[i];
            
            int node_count = layer->nodes->length;
            if (node_count == 0) continue;
            
            NodeWithBarycenter* nodes_with_bc = (NodeWithBarycenter*)malloc(
                node_count * sizeof(NodeWithBarycenter));
            
            for (int j = 0; j < node_count; j++) {
                LayoutNode* node = (LayoutNode*)layer->nodes->data[j];
                nodes_with_bc[j].node = node;
                nodes_with_bc[j].barycenter = compute_barycenter(node, false);
            }
            
            qsort(nodes_with_bc, node_count, sizeof(NodeWithBarycenter),
                  compare_by_barycenter);
            
            arraylist_clear(layer->nodes);
            for (int j = 0; j < node_count; j++) {
                LayoutNode* node = nodes_with_bc[j].node;
                node->order = j;
                arraylist_append(layer->nodes, node);
            }
            
            free(nodes_with_bc);
        }
        
        // count crossings after up sweep
        crossings = 0;
        for (int i = 0; i < graph->layers->length - 1; i++) {
            LayoutLayer* layer1 = (LayoutLayer*)graph->layers->data[i];
            LayoutLayer* layer2 = (LayoutLayer*)graph->layers->data[i + 1];
            crossings += count_crossings_between_layers(layer1, layer2);
        }
        
        if (crossings < best_crossings) {
            best_crossings = crossings;
            improved = true;
        }
        
        iteration++;
        
        // early exit if no improvement
        if (!improved || best_crossings == 0) {
            break;
        }
    }
    
    log_debug("dagre: crossing reduction complete (%d -> %d crossings, %d iterations)",
              initial_crossings, best_crossings, iteration);
}

// ============================================================================
// Phase 4: Coordinate Assignment (Simple Grid-Based)
// ============================================================================

void dagre_assign_coordinates(LayoutGraph* graph, GraphLayoutOptions* opts) {
    log_debug("dagre: assigning coordinates");
    
    float rank_sep = opts->rank_sep;
    float node_sep = opts->node_sep;
    
    // assign y coordinates based on rank
    for (int i = 0; i < graph->layers->length; i++) {
        LayoutLayer* layer = (LayoutLayer*)graph->layers->data[i];
        float y = layer->rank * rank_sep;
        
        // compute total width needed for this layer
        float total_width = 0.0f;
        for (int j = 0; j < layer->nodes->length; j++) {
            LayoutNode* node = (LayoutNode*)layer->nodes->data[j];
            total_width += node->width;
            if (j > 0) total_width += node_sep;
        }
        
        // center the layer
        float x = -total_width / 2.0f;
        
        // assign x coordinates
        for (int j = 0; j < layer->nodes->length; j++) {
            LayoutNode* node = (LayoutNode*)layer->nodes->data[j];
            
            node->x = x + node->width / 2.0f;
            node->y = y;
            
            x += node->width + node_sep;
        }
    }
    
    // compute graph bounds
    graph->min_x = FLT_MAX;
    graph->min_y = FLT_MAX;
    graph->max_x = -FLT_MAX;
    graph->max_y = -FLT_MAX;
    
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        
        float left = node->x - node->width / 2.0f;
        float right = node->x + node->width / 2.0f;
        float top = node->y - node->height / 2.0f;
        float bottom = node->y + node->height / 2.0f;
        
        if (left < graph->min_x) graph->min_x = left;
        if (right > graph->max_x) graph->max_x = right;
        if (top < graph->min_y) graph->min_y = top;
        if (bottom > graph->max_y) graph->max_y = bottom;
    }
    
    // shift all coordinates so min is at (0, 0)
    float offset_x = -graph->min_x;
    float offset_y = -graph->min_y;
    
    for (int i = 0; i < graph->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)graph->nodes->data[i];
        node->x += offset_x;
        node->y += offset_y;
    }
    
    graph->min_x = 0;
    graph->min_y = 0;
    graph->max_x += offset_x;
    graph->max_y += offset_y;
    
    log_debug("dagre: coordinate assignment complete (bounds: %.1f x %.1f)",
              graph->max_x, graph->max_y);
}

// ============================================================================
// Phase 5: Edge Routing (Straight Lines)
// ============================================================================

void dagre_route_edges(LayoutGraph* graph, bool use_splines) {
    log_debug("dagre: routing edges (straight lines)");
    
    for (int i = 0; i < graph->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)graph->edges->data[i];
        
        // simple straight line from source to target
        Point2D* start = (Point2D*)calloc(1, sizeof(Point2D));
        start->x = edge->from_node->x;
        start->y = edge->from_node->y;
        
        Point2D* end = (Point2D*)calloc(1, sizeof(Point2D));
        end->x = edge->to_node->x;
        end->y = edge->to_node->y;
        
        arraylist_append(edge->path_points, start);
        arraylist_append(edge->path_points, end);
    }
    
    log_debug("dagre: edge routing complete");
}

// ============================================================================
// Main Dagre Algorithm
// ============================================================================

void layout_graph_dagre(LayoutGraph* graph, GraphLayoutOptions* opts) {
    log_info("starting dagre layout algorithm");
    
    // phase 1: assign ranks (layers)
    dagre_assign_ranks(graph);
    
    // phase 2: create layer structures
    dagre_create_layers(graph);
    
    // phase 3: reduce crossings
    dagre_reduce_crossings(graph, opts->max_iterations);
    
    // phase 4: assign x,y coordinates
    dagre_assign_coordinates(graph, opts);
    
    // phase 5: route edges
    dagre_route_edges(graph, opts->use_splines);
    
    log_info("dagre layout complete");
}
