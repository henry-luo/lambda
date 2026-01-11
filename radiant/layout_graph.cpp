#include "layout_graph.hpp"
#include "graph_dagre.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

using namespace lambda;

// Create default layout options
GraphLayoutOptions* create_default_layout_options() {
    GraphLayoutOptions* opts = (GraphLayoutOptions*)calloc(1, sizeof(GraphLayoutOptions));
    opts->algorithm = "dagre";
    opts->direction = "TB";
    opts->node_sep = 50.0f;
    opts->rank_sep = 50.0f;
    opts->edge_sep = 10.0f;
    opts->use_splines = false;
    opts->max_iterations = 100;
    return opts;
}

// Extract layout algorithm from graph attributes
static const char* get_layout_algorithm(Element* graph) {
    if (!graph || !graph->attrs.map) {
        return "dagre";
    }
    
    Item layout_item = map_get(graph->attrs.map, "layout");
    if (is_string(layout_item)) {
        const char* layout = layout_item.string_ptr->chars;
        
        // map common layout names
        if (strcmp(layout, "hierarchical") == 0 || strcmp(layout, "layered") == 0) {
            return "dagre";
        }
        if (strcmp(layout, "dagre") == 0 || strcmp(layout, "dot") == 0) {
            return "dagre";
        }
    }
    
    return "dagre"; // default
}

// Extract direction from graph attributes
static const char* get_layout_direction(Element* graph) {
    if (!graph || !graph->attrs.map) {
        return "TB";
    }
    
    Item direction_item = map_get(graph->attrs.map, "direction");
    if (is_string(direction_item)) {
        return direction_item.string_ptr->chars;
    }
    
    return "TB"; // default top-to-bottom
}

// Build internal LayoutGraph from Lambda graph Element
static LayoutGraph* build_layout_graph(Element* graph) {
    LayoutGraph* lg = (LayoutGraph*)calloc(1, sizeof(LayoutGraph));
    lg->nodes = arraylist_create(32);
    lg->edges = arraylist_create(32);
    lg->layers = arraylist_create(10);
    
    // determine if directed
    Item directed_item = map_get(graph->attrs.map, "directed");
    lg->is_directed = is_string(directed_item) && 
                      strcmp(directed_item.string_ptr->chars, "true") == 0;
    
    Item type_item = map_get(graph->attrs.map, "type");
    lg->type = is_string(type_item) ? type_item.string_ptr->chars : "directed";
    
    MarkReader reader(graph);
    
    // extract nodes
    Item children = reader.getChildren();
    if (is_list(children)) {
        List* child_list = children.list;
        for (size_t i = 0; i < child_list->length; i++) {
            Item child_item = list_get(child_list, i);
            if (!is_element(child_item)) continue;
            
            Element* child = child_item.element;
            const char* tag = child->tag ? child->tag->chars : "";
            
            if (strcmp(tag, "node") == 0) {
                LayoutNode* node = (LayoutNode*)calloc(1, sizeof(LayoutNode));
                node->in_edges = arraylist_create(4);
                node->out_edges = arraylist_create(4);
                
                // extract node properties
                Item id_item = map_get(child->attrs.map, "id");
                node->id = is_string(id_item) ? id_item.string_ptr->chars : "";
                
                Item label_item = map_get(child->attrs.map, "label");
                node->label = is_string(label_item) ? label_item.string_ptr->chars : node->id;
                
                Item shape_item = map_get(child->attrs.map, "shape");
                node->shape = is_string(shape_item) ? shape_item.string_ptr->chars : "box";
                
                Item fill_item = map_get(child->attrs.map, "fill");
                node->fill = is_string(fill_item) ? fill_item.string_ptr->chars : "lightblue";
                
                Item stroke_item = map_get(child->attrs.map, "stroke");
                node->stroke = is_string(stroke_item) ? stroke_item.string_ptr->chars : "black";
                
                // default dimensions (will be measured properly later)
                node->width = 80.0f;
                node->height = 40.0f;
                
                arraylist_add(lg->nodes, node);
            }
        }
    }
    
    // extract edges (second pass after all nodes are created)
    reader = MarkReader(graph);
    children = reader.getChildren();
    if (is_list(children)) {
        List* child_list = children.list;
        for (size_t i = 0; i < child_list->length; i++) {
            Item child_item = list_get(child_list, i);
            if (!is_element(child_item)) continue;
            
            Element* child = child_item.element;
            const char* tag = child->tag ? child->tag->chars : "";
            
            if (strcmp(tag, "edge") == 0) {
                LayoutEdge* edge = (LayoutEdge*)calloc(1, sizeof(LayoutEdge));
                edge->path_points = arraylist_create(4);
                
                Item from_item = map_get(child->attrs.map, "from");
                edge->from_id = is_string(from_item) ? from_item.string_ptr->chars : "";
                
                Item to_item = map_get(child->attrs.map, "to");
                edge->to_id = is_string(to_item) ? to_item.string_ptr->chars : "";
                
                Item label_item = map_get(child->attrs.map, "label");
                edge->label = is_string(label_item) ? label_item.string_ptr->chars : nullptr;
                
                Item style_item = map_get(child->attrs.map, "style");
                edge->style = is_string(style_item) ? style_item.string_ptr->chars : "solid";
                
                edge->directed = lg->is_directed;
                
                // link to nodes (linear search)
                edge->from_node = nullptr;
                edge->to_node = nullptr;
                for (size_t j = 0; j < lg->nodes->length; j++) {
                    LayoutNode* n = (LayoutNode*)arraylist_at(lg->nodes, j);
                    if (strcmp(n->id, edge->from_id) == 0) {
                        edge->from_node = n;
                    }
                    if (strcmp(n->id, edge->to_id) == 0) {
                        edge->to_node = n;
                    }
                }
                
                if (edge->from_node && edge->to_node) {
                    arraylist_add(edge->from_node->out_edges, edge);
                    arraylist_add(edge->to_node->in_edges, edge);
                    arraylist_add(lg->edges, edge);
                } else {
                    log_warn("edge references non-existent nodes: %s -> %s", 
                            edge->from_id, edge->to_id);
                    arraylist_destroy(edge->path_points);
                    free(edge);
                }
            }
        }
    }
    
    log_info("built layout graph: %zu nodes, %zu edges", 
             lg->nodes->length, lg->edges->length);
    
    return lg;
}

// Free internal layout graph
static void free_layout_graph_internal(LayoutGraph* lg) {
    if (!lg) return;
    
    // free nodes
    for (size_t i = 0; i < lg->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)arraylist_at(lg->nodes, i);
        arraylist_destroy(node->in_edges);
        arraylist_destroy(node->out_edges);
        free(node);
    }
    arraylist_destroy(lg->nodes);
    
    // free edges
    for (size_t i = 0; i < lg->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)arraylist_at(lg->edges, i);
        
        // free path points
        for (size_t j = 0; j < edge->path_points->length; j++) {
            Point2D* pt = (Point2D*)arraylist_at(edge->path_points, j);
            free(pt);
        }
        arraylist_destroy(edge->path_points);
        free(edge);
    }
    arraylist_destroy(lg->edges);
    
    // free layers
    for (size_t i = 0; i < lg->layers->length; i++) {
        LayoutLayer* layer = (LayoutLayer*)arraylist_at(lg->layers, i);
        arraylist_destroy(layer->nodes);
        free(layer);
    }
    arraylist_destroy(lg->layers);
    
    free(lg);
}

// Convert internal LayoutGraph to GraphLayout result
static GraphLayout* extract_graph_layout(LayoutGraph* lg, GraphLayoutOptions* opts) {
    GraphLayout* layout = (GraphLayout*)calloc(1, sizeof(GraphLayout));
    layout->node_positions = arraylist_create(lg->nodes->length);
    layout->edge_paths = arraylist_create(lg->edges->length);
    
    layout->node_spacing_x = opts->node_sep;
    layout->node_spacing_y = opts->rank_sep;
    layout->edge_spacing = opts->edge_sep;
    layout->algorithm = opts->algorithm;
    layout->direction = opts->direction;
    
    // extract node positions
    for (size_t i = 0; i < lg->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)arraylist_at(lg->nodes, i);
        
        NodePosition* pos = (NodePosition*)calloc(1, sizeof(NodePosition));
        pos->node_id = node->id;
        pos->x = node->x;
        pos->y = node->y;
        pos->width = node->width;
        pos->height = node->height;
        pos->rank = node->rank;
        pos->order = node->order;
        
        arraylist_add(layout->node_positions, pos);
    }
    
    // extract edge paths
    for (size_t i = 0; i < lg->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)arraylist_at(lg->edges, i);
        
        EdgePath* path = (EdgePath*)calloc(1, sizeof(EdgePath));
        path->from_id = edge->from_id;
        path->to_id = edge->to_id;
        path->points = arraylist_create(edge->path_points->length);
        path->is_bezier = opts->use_splines;
        path->directed = edge->directed;
        
        // copy points
        for (size_t j = 0; j < edge->path_points->length; j++) {
            Point2D* src_pt = (Point2D*)arraylist_at(edge->path_points, j);
            Point2D* dst_pt = (Point2D*)calloc(1, sizeof(Point2D));
            dst_pt->x = src_pt->x;
            dst_pt->y = src_pt->y;
            arraylist_add(path->points, dst_pt);
        }
        
        arraylist_add(layout->edge_paths, path);
    }
    
    // compute graph bounds
    layout->graph_width = lg->max_x - lg->min_x;
    layout->graph_height = lg->max_y - lg->min_y;
    
    return layout;
}

// Main layout functions
GraphLayout* layout_graph(Element* graph) {
    const char* algorithm = get_layout_algorithm(graph);
    return layout_graph_with_algorithm(graph, algorithm);
}

GraphLayout* layout_graph_with_algorithm(Element* graph, const char* algorithm) {
    GraphLayoutOptions* opts = create_default_layout_options();
    opts->algorithm = algorithm;
    opts->direction = get_layout_direction(graph);
    
    GraphLayout* result = layout_graph_with_options(graph, opts);
    free(opts);
    return result;
}

GraphLayout* layout_graph_with_options(Element* graph, GraphLayoutOptions* opts) {
    if (!graph) {
        log_error("layout_graph: null graph element");
        return nullptr;
    }
    
    log_info("laying out graph with algorithm: %s, direction: %s", 
             opts->algorithm, opts->direction);
    
    // build internal graph representation
    LayoutGraph* lg = build_layout_graph(graph);
    
    // dispatch to algorithm
    if (strcmp(opts->algorithm, "dagre") == 0 || strcmp(opts->algorithm, "dot") == 0) {
        layout_graph_dagre(lg, opts);
    } else {
        log_error("unknown layout algorithm: %s", opts->algorithm);
        free_layout_graph_internal(lg);
        return nullptr;
    }
    
    // extract results
    GraphLayout* result = extract_graph_layout(lg, opts);
    
    // cleanup
    free_layout_graph_internal(lg);
    
    log_info("layout complete: %.1f x %.1f", result->graph_width, result->graph_height);
    
    return result;
}

// Free layout resources
void free_graph_layout(GraphLayout* layout) {
    if (!layout) return;
    
    // free node positions
    for (size_t i = 0; i < layout->node_positions->length; i++) {
        NodePosition* pos = (NodePosition*)arraylist_at(layout->node_positions, i);
        free(pos);
    }
    arraylist_destroy(layout->node_positions);
    
    // free edge paths
    for (size_t i = 0; i < layout->edge_paths->length; i++) {
        EdgePath* path = (EdgePath*)arraylist_at(layout->edge_paths, i);
        
        for (size_t j = 0; j < path->points->length; j++) {
            Point2D* pt = (Point2D*)arraylist_at(path->points, j);
            free(pt);
        }
        arraylist_destroy(path->points);
        free(path);
    }
    arraylist_destroy(layout->edge_paths);
    
    free(layout);
}
