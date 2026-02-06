#include "layout_graph.hpp"
#include "graph_dagre.hpp"
#include "graph_edge_utils.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Create default layout options
GraphLayoutOptions* create_default_layout_options() {
    GraphLayoutOptions* opts = (GraphLayoutOptions*)calloc(1, sizeof(GraphLayoutOptions));
    opts->algorithm = "dagre";
    opts->direction = "TB";
    opts->node_sep = 60.0f;
    opts->rank_sep = 80.0f;
    opts->edge_sep = 10.0f;
    opts->use_splines = false;
    opts->max_iterations = 100;
    return opts;
}

// helper to get string attribute from element
static const char* get_string_attr(Element* elem, const char* key, const char* default_val) {
    if (!elem) return default_val;

    ElementReader reader(elem);
    ItemReader attr_reader = reader.get_attr(key);
    if (attr_reader.isString()) {
        return attr_reader.cstring();
    }
    return default_val;
}

// Extract layout algorithm from graph attributes
static const char* get_layout_algorithm(Element* graph) {
    const char* layout = get_string_attr(graph, "layout", nullptr);
    if (!layout) return "dagre";

    // map common layout names
    if (strcmp(layout, "hierarchical") == 0 || strcmp(layout, "layered") == 0) {
        return "dagre";
    }
    if (strcmp(layout, "dagre") == 0 || strcmp(layout, "dot") == 0) {
        return "dagre";
    }

    return "dagre"; // default
}

// Extract direction from graph attributes
static const char* get_layout_direction(Element* graph) {
    const char* direction = get_string_attr(graph, "direction", nullptr);
    if (direction) return direction;
    return "TB"; // default top-to-bottom
}

// Forward declaration
static void extract_nodes_recursive(ElementReader& reader, LayoutGraph* lg);
static void extract_edges_recursive(ElementReader& reader, LayoutGraph* lg);
static void extract_subgraphs_recursive(ElementReader& reader, LayoutGraph* lg);

// Helper to extract a single node from an element reader
static LayoutNode* extract_single_node(ElementReader& child_reader) {
    LayoutNode* node = (LayoutNode*)calloc(1, sizeof(LayoutNode));
    node->in_edges = arraylist_new(4);
    node->out_edges = arraylist_new(4);

    // extract node properties
    ItemReader id_reader = child_reader.get_attr("id");
    node->id = id_reader.isString() ? id_reader.cstring() : "";

    ItemReader label_reader = child_reader.get_attr("label");
    node->label = label_reader.isString() ? label_reader.cstring() : node->id;

    ItemReader shape_reader = child_reader.get_attr("shape");
    node->shape = shape_reader.isString() ? shape_reader.cstring() : "box";

    ItemReader fill_reader = child_reader.get_attr("fill");
    node->fill = fill_reader.isString() ? fill_reader.cstring() : "lightblue";

    ItemReader stroke_reader = child_reader.get_attr("stroke");
    node->stroke = stroke_reader.isString() ? stroke_reader.cstring() : "black";

    // default dimensions
    node->width = 80.0f;
    node->height = 40.0f;

    return node;
}

// Recursively extract nodes from element and its subgraphs
static void extract_nodes_recursive(ElementReader& reader, LayoutGraph* lg) {
    ElementReader::ChildIterator children = reader.children();
    ItemReader child_item;

    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;

        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag) continue;

        if (strcmp(tag, "node") == 0) {
            LayoutNode* node = extract_single_node(child_reader);
            arraylist_append(lg->nodes, node);
        } else if (strcmp(tag, "subgraph") == 0) {
            // recurse into subgraph to extract its nodes
            extract_nodes_recursive(child_reader, lg);
        }
    }
}

// Recursively extract edges from element and its subgraphs
static void extract_edges_recursive(ElementReader& reader, LayoutGraph* lg) {
    ElementReader::ChildIterator children = reader.children();
    ItemReader child_item;

    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;

        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag) continue;

        if (strcmp(tag, "edge") == 0) {
            LayoutEdge* edge = (LayoutEdge*)calloc(1, sizeof(LayoutEdge));
            edge->path_points = arraylist_new(4);

            ItemReader from_reader = child_reader.get_attr("from");
            edge->from_id = from_reader.isString() ? from_reader.cstring() : "";

            ItemReader to_reader = child_reader.get_attr("to");
            edge->to_id = to_reader.isString() ? to_reader.cstring() : "";

            ItemReader label_reader = child_reader.get_attr("label");
            edge->label = label_reader.isString() ? label_reader.cstring() : nullptr;

            ItemReader style_reader = child_reader.get_attr("style");
            edge->style = style_reader.isString() ? style_reader.cstring() : "solid";

            ItemReader arrow_start_reader = child_reader.get_attr("arrow-start");
            edge->arrow_start = arrow_start_reader.isBool() ? arrow_start_reader.asBool() : false;

            ItemReader arrow_end_reader = child_reader.get_attr("arrow-end");
            edge->arrow_end = arrow_end_reader.isBool() ? arrow_end_reader.asBool() : lg->is_directed;

            edge->directed = lg->is_directed;

            // link to nodes (linear search)
            edge->from_node = nullptr;
            edge->to_node = nullptr;
            for (int j = 0; j < lg->nodes->length; j++) {
                LayoutNode* n = (LayoutNode*)lg->nodes->data[j];
                if (strcmp(n->id, edge->from_id) == 0) {
                    edge->from_node = n;
                }
                if (strcmp(n->id, edge->to_id) == 0) {
                    edge->to_node = n;
                }
            }

            if (edge->from_node && edge->to_node) {
                arraylist_append(edge->from_node->out_edges, edge);
                arraylist_append(edge->to_node->in_edges, edge);
                arraylist_append(lg->edges, edge);
            } else {
                log_warn("edge references non-existent nodes: %s -> %s",
                        edge->from_id, edge->to_id);
                arraylist_free(edge->path_points);
                free(edge);
            }
        } else if (strcmp(tag, "subgraph") == 0) {
            // recurse into subgraph to extract its edges
            extract_edges_recursive(child_reader, lg);
        }
    }
}

// Forward declaration for recursive node ID collection
static void collect_node_ids_recursive(ElementReader& reader, ArrayList* node_ids);

// Collect node IDs recursively from element and nested subgraphs
static void collect_node_ids_recursive(ElementReader& reader, ArrayList* node_ids) {
    ElementReader::ChildIterator children = reader.children();
    ItemReader child_item;

    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;

        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag) continue;

        if (strcmp(tag, "node") == 0) {
            ItemReader id_reader = child_reader.get_attr("id");
            if (id_reader.isString()) {
                arraylist_append(node_ids, (void*)id_reader.cstring());
            }
        } else if (strcmp(tag, "subgraph") == 0) {
            // recurse into nested subgraph
            collect_node_ids_recursive(child_reader, node_ids);
        }
    }
}

// Recursively extract subgraphs (collects all subgraphs at all levels)
static void extract_subgraphs_recursive(ElementReader& reader, LayoutGraph* lg) {
    ElementReader::ChildIterator children = reader.children();
    ItemReader child_item;

    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;

        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag || strcmp(tag, "subgraph") != 0) continue;

        LayoutSubgraph* sg = (LayoutSubgraph*)calloc(1, sizeof(LayoutSubgraph));
        sg->node_ids = arraylist_new(8);
        sg->subgraphs = arraylist_new(2);

        ItemReader id_reader = child_reader.get_attr("id");
        sg->id = id_reader.isString() ? id_reader.cstring() : "";

        ItemReader label_reader = child_reader.get_attr("label");
        sg->label = label_reader.isString() ? label_reader.cstring() : sg->id;

        ItemReader direction_reader = child_reader.get_attr("direction");
        sg->direction = direction_reader.isString() ? direction_reader.cstring() : nullptr;

        sg->fill = nullptr;
        sg->stroke = nullptr;
        sg->padding = 15.0f;
        sg->label_height = 20.0f;

        // collect node IDs recursively (including from nested subgraphs)
        collect_node_ids_recursive(child_reader, sg->node_ids);

        arraylist_append(lg->subgraphs, sg);
        log_debug("extracted subgraph '%s' with %d nodes", sg->id, sg->node_ids->length);

        // also recurse to extract nested subgraphs as separate entries
        extract_subgraphs_recursive(child_reader, lg);
    }
}

// Build internal LayoutGraph from Lambda graph Element
static LayoutGraph* build_layout_graph(Element* graph) {
    LayoutGraph* lg = (LayoutGraph*)calloc(1, sizeof(LayoutGraph));
    lg->nodes = arraylist_new(32);
    lg->edges = arraylist_new(32);
    lg->layers = arraylist_new(10);
    lg->subgraphs = arraylist_new(4);

    // determine if directed
    const char* directed_str = get_string_attr(graph, "directed", "true");
    lg->is_directed = (strcmp(directed_str, "true") == 0);
    lg->type = get_string_attr(graph, "type", "directed");

    // use ElementReader to traverse children
    ElementReader graph_reader(graph);

    // extract all nodes recursively (including from subgraphs)
    extract_nodes_recursive(graph_reader, lg);

    // extract all edges recursively (including from subgraphs)
    extract_edges_recursive(graph_reader, lg);

    // extract subgraph definitions
    extract_subgraphs_recursive(graph_reader, lg);

    log_info("built layout graph: %d nodes, %d edges, %d subgraphs",
             lg->nodes->length, lg->edges->length, lg->subgraphs->length);

    return lg;
}

// Free internal layout graph
static void free_layout_graph_internal(LayoutGraph* lg) {
    if (!lg) return;

    // free nodes
    for (int i = 0; i < lg->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)lg->nodes->data[i];
        arraylist_free(node->in_edges);
        arraylist_free(node->out_edges);
        free(node);
    }
    arraylist_free(lg->nodes);

    // free edges
    for (int i = 0; i < lg->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)lg->edges->data[i];

        // free path points
        for (int j = 0; j < edge->path_points->length; j++) {
            Point2D* pt = (Point2D*)edge->path_points->data[j];
            free(pt);
        }
        arraylist_free(edge->path_points);
        free(edge);
    }
    arraylist_free(lg->edges);

    // free layers
    for (int i = 0; i < lg->layers->length; i++) {
        LayoutLayer* layer = (LayoutLayer*)lg->layers->data[i];
        arraylist_free(layer->nodes);
        free(layer);
    }
    arraylist_free(lg->layers);

    // free subgraphs
    if (lg->subgraphs) {
        for (int i = 0; i < lg->subgraphs->length; i++) {
            LayoutSubgraph* sg = (LayoutSubgraph*)lg->subgraphs->data[i];
            if (sg->node_ids) arraylist_free(sg->node_ids);
            if (sg->subgraphs) arraylist_free(sg->subgraphs);
            free(sg);
        }
        arraylist_free(lg->subgraphs);
    }

    free(lg);
}

// Convert internal LayoutGraph to GraphLayout result
static GraphLayout* extract_graph_layout(LayoutGraph* lg, GraphLayoutOptions* opts) {
    GraphLayout* layout = (GraphLayout*)calloc(1, sizeof(GraphLayout));
    layout->node_positions = arraylist_new(lg->nodes->length > 0 ? lg->nodes->length : 1);
    layout->edge_paths = arraylist_new(lg->edges->length > 0 ? lg->edges->length : 1);
    layout->subgraph_positions = arraylist_new(lg->subgraphs->length > 0 ? lg->subgraphs->length : 1);

    layout->node_spacing_x = opts->node_sep;
    layout->node_spacing_y = opts->rank_sep;
    layout->edge_spacing = opts->edge_sep;
    layout->algorithm = opts->algorithm;
    layout->direction = opts->direction;

    // extract node positions
    for (int i = 0; i < lg->nodes->length; i++) {
        LayoutNode* node = (LayoutNode*)lg->nodes->data[i];

        NodePosition* pos = (NodePosition*)calloc(1, sizeof(NodePosition));
        pos->node_id = node->id;
        pos->x = node->x;
        pos->y = node->y;
        pos->width = node->width;
        pos->height = node->height;
        pos->rank = node->rank;
        pos->order = node->order;

        arraylist_append(layout->node_positions, pos);
    }

    // extract edge paths
    for (int i = 0; i < lg->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)lg->edges->data[i];

        EdgePath* path = (EdgePath*)calloc(1, sizeof(EdgePath));
        path->from_id = edge->from_id;
        path->to_id = edge->to_id;
        path->points = arraylist_new(edge->path_points->length > 0 ? edge->path_points->length : 1);
        path->is_bezier = opts->use_splines;
        path->directed = edge->directed;
        path->edge_style = edge->style ? edge->style : "solid";
        path->arrow_start = edge->arrow_start;
        path->arrow_end = edge->arrow_end;

        // copy points
        for (int j = 0; j < edge->path_points->length; j++) {
            Point2D* src_pt = (Point2D*)edge->path_points->data[j];
            Point2D* dst_pt = (Point2D*)calloc(1, sizeof(Point2D));
            dst_pt->x = src_pt->x;
            dst_pt->y = src_pt->y;
            arraylist_append(path->points, dst_pt);
        }

        arraylist_append(layout->edge_paths, path);
    }

    // extract subgraph positions - compute bounds from member nodes
    for (int i = 0; i < lg->subgraphs->length; i++) {
        LayoutSubgraph* sg = (LayoutSubgraph*)lg->subgraphs->data[i];

        // compute bounding box from member nodes
        float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
        bool has_nodes = false;

        for (int j = 0; j < sg->node_ids->length; j++) {
            const char* node_id = (const char*)sg->node_ids->data[j];

            // find node position
            for (int k = 0; k < lg->nodes->length; k++) {
                LayoutNode* node = (LayoutNode*)lg->nodes->data[k];
                if (strcmp(node->id, node_id) == 0) {
                    float nx1 = node->x - node->width / 2.0f;
                    float ny1 = node->y - node->height / 2.0f;
                    float nx2 = node->x + node->width / 2.0f;
                    float ny2 = node->y + node->height / 2.0f;

                    if (nx1 < min_x) min_x = nx1;
                    if (ny1 < min_y) min_y = ny1;
                    if (nx2 > max_x) max_x = nx2;
                    if (ny2 > max_y) max_y = ny2;
                    has_nodes = true;
                    break;
                }
            }
        }

        if (!has_nodes) {
            // empty subgraph, skip
            continue;
        }

        // add padding and label height
        float padding = sg->padding;
        float label_height = sg->label_height;

        SubgraphPosition* sgpos = (SubgraphPosition*)calloc(1, sizeof(SubgraphPosition));
        sgpos->subgraph_id = sg->id;
        sgpos->label = sg->label;
        sgpos->x = min_x - padding;
        sgpos->y = min_y - padding - label_height;
        sgpos->width = (max_x - min_x) + 2 * padding;
        sgpos->height = (max_y - min_y) + 2 * padding + label_height;
        sgpos->label_height = label_height;

        arraylist_append(layout->subgraph_positions, sgpos);
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

    // post-process edges for better visual appearance
    post_process_edges(lg, opts->direction);

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
    for (int i = 0; i < layout->node_positions->length; i++) {
        NodePosition* pos = (NodePosition*)layout->node_positions->data[i];
        free(pos);
    }
    arraylist_free(layout->node_positions);

    // free edge paths
    for (int i = 0; i < layout->edge_paths->length; i++) {
        EdgePath* path = (EdgePath*)layout->edge_paths->data[i];

        for (int j = 0; j < path->points->length; j++) {
            Point2D* pt = (Point2D*)path->points->data[j];
            free(pt);
        }
        arraylist_free(path->points);
        free(path);
    }
    arraylist_free(layout->edge_paths);

    // free subgraph positions
    if (layout->subgraph_positions) {
        for (int i = 0; i < layout->subgraph_positions->length; i++) {
            SubgraphPosition* sgpos = (SubgraphPosition*)layout->subgraph_positions->data[i];
            free(sgpos);
        }
        arraylist_free(layout->subgraph_positions);
    }

    free(layout);
}
