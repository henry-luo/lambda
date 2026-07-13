#include "layout_graph.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include <string.h>
#include "../lib/mem.h"

// Create default layout options
GraphLayoutOptions* create_default_layout_options() {
    GraphLayoutOptions* opts = (GraphLayoutOptions*)mem_calloc(1, sizeof(GraphLayoutOptions), MEM_CAT_LAYOUT);
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
static lam::SessionPtr<LayoutNode> extract_single_node(ElementReader& child_reader) {
    lam::SessionPtr<LayoutNode> node = lam::session_make<LayoutNode>(MEM_CAT_LAYOUT);
    if (!node) {
        log_error("graph_extract_node_alloc_failed");
        return lam::SessionPtr<LayoutNode>();
    }
    node->in_edges = graph_list_new<LayoutEdgeRefList>(4);
    node->out_edges = graph_list_new<LayoutEdgeRefList>(4);

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

    return static_cast<lam::SessionPtr<LayoutNode>&&>(node);
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
            lam::SessionPtr<LayoutNode> node = extract_single_node(child_reader);
            if (node) {
                lg->nodes->get().append(static_cast<lam::SessionPtr<LayoutNode>&&>(node));
            }
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
            lam::SessionPtr<LayoutEdge> edge = lam::session_make<LayoutEdge>(MEM_CAT_LAYOUT);
            if (!edge) {
                log_error("graph_extract_edge_alloc_failed");
                continue;
            }
            edge->path_points = point2d_list_new(4);

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
            for (size_t j = 0; j < lg->nodes->get().size(); j++) {
                LayoutNode* n = lg->nodes->get()[j].get();
                if (strcmp(n->id, edge->from_id) == 0) {
                    edge->from_node = n;
                }
                if (strcmp(n->id, edge->to_id) == 0) {
                    edge->to_node = n;
                }
            }

            if (edge->from_node && edge->to_node) {
                LayoutEdge* edge_ref = edge.get();
                edge->from_node->out_edges->append(edge_ref);
                edge->to_node->in_edges->append(edge_ref);
                lg->edges->get().append(static_cast<lam::SessionPtr<LayoutEdge>&&>(edge));
            } else {
                log_warn("edge references non-existent nodes: %s -> %s",
                        edge->from_id, edge->to_id);
                point2d_list_free(edge->path_points);
                edge->path_points = nullptr;
            }
        } else if (strcmp(tag, "subgraph") == 0) {
            // recurse into subgraph to extract its edges
            extract_edges_recursive(child_reader, lg);
        }
    }
}

// Forward declaration for recursive node ID collection
static void collect_node_ids_recursive(ElementReader& reader, GraphNodeIdList* node_ids);

// Collect node IDs recursively from element and nested subgraphs
static void collect_node_ids_recursive(ElementReader& reader, GraphNodeIdList* node_ids) {
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
                node_ids->append(id_reader.cstring());
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

        lam::SessionPtr<LayoutSubgraph> sg = lam::session_make<LayoutSubgraph>(MEM_CAT_LAYOUT);
        if (!sg) {
            log_error("graph_extract_subgraph_alloc_failed");
            continue;
        }
        sg->node_ids = graph_list_new<GraphNodeIdList>(8);
        sg->subgraphs = graph_list_new<LayoutSubgraphRefList>(2);

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

        log_debug("extracted subgraph '%s' with %zu nodes", sg->id, sg->node_ids->size());
        lg->subgraphs->get().append(static_cast<lam::SessionPtr<LayoutSubgraph>&&>(sg));

        // also recurse to extract nested subgraphs as separate entries
        extract_subgraphs_recursive(child_reader, lg);
    }
}

// Build internal LayoutGraph from Lambda graph Element
static LayoutGraph* build_layout_graph(Element* graph) {
    LayoutGraph* lg = (LayoutGraph*)mem_calloc(1, sizeof(LayoutGraph), MEM_CAT_LAYOUT);
    lg->nodes = graph_list_new<PersistentLayoutNodeList>(32);
    lg->edges = graph_list_new<PersistentLayoutEdgeList>(32);
    lg->layers = graph_list_new<PersistentLayoutLayerList>(10);
    lg->subgraphs = graph_list_new<PersistentLayoutSubgraphList>(4);

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

    log_info("built layout graph: %zu nodes, %zu edges, %zu subgraphs",
             lg->nodes->get().size(), lg->edges->get().size(), lg->subgraphs->get().size());

    return lg;
}

// Free internal layout graph
static void free_layout_graph_internal(LayoutGraph* lg) {
    if (!lg) return;

    // free nodes
    for (size_t i = 0; i < lg->nodes->get().size(); i++) {
        LayoutNode* node = lg->nodes->get()[i].get();
        graph_list_free(node->in_edges);
        graph_list_free(node->out_edges);
    }
    graph_list_free(lg->nodes);

    // free edges
    for (size_t i = 0; i < lg->edges->get().size(); i++) {
        LayoutEdge* edge = lg->edges->get()[i].get();

        point2d_list_free(edge->path_points);
    }
    graph_list_free(lg->edges);

    // free layers
    for (size_t i = 0; i < lg->layers->get().size(); i++) {
        LayoutLayer* layer = lg->layers->get()[i].get();
        graph_list_free(layer->nodes);
    }
    graph_list_free(lg->layers);

    // free subgraphs
    if (lg->subgraphs) {
        for (size_t i = 0; i < lg->subgraphs->get().size(); i++) {
            LayoutSubgraph* sg = lg->subgraphs->get()[i].get();
            graph_list_free(sg->node_ids);
            graph_list_free(sg->subgraphs);
        }
        graph_list_free(lg->subgraphs);
    }

    mem_free(lg);
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
    mem_free(opts);
    return result;
}

GraphLayout* layout_graph_with_options(Element* graph, GraphLayoutOptions* opts) {
    if (!graph) {
        log_error("layout_graph: null graph element");
        return nullptr;
    }

    log_info("laying out graph with algorithm: %s, direction: %s",
             opts->algorithm, opts->direction);

    LayoutGraph* lg = build_layout_graph(graph);

    // graph coordinate assignment has moved to lambda/package/graph; keep the
    // old C ABI as a clean failure until the CLI graph render path calls Lambda.
    log_error("layout_graph: C graph layout algorithm removed; use lambda.package.graph.graph");
    free_layout_graph_internal(lg);
    return nullptr;
}

// Free layout resources
void free_graph_layout(GraphLayout* layout) {
    if (!layout) return;

    // free node positions
    graph_list_free(layout->node_positions);

    // free edge paths
    for (size_t i = 0; i < layout->edge_paths->get().size(); i++) {
        EdgePath* path = layout->edge_paths->get()[i].get();

        point2d_list_free(path->points);
    }
    graph_list_free(layout->edge_paths);

    // free subgraph positions
    graph_list_free(layout->subgraph_positions);

    mem_free(layout);
}
