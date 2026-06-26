#ifndef RADIANT_GRAPH_LAYOUT_TYPES_HPP
#define RADIANT_GRAPH_LAYOUT_TYPES_HPP

#include "../lib/hashmap.h"
#include "../lib/arraylist.hpp"

// Forward declaration for theme support
struct DiagramTheme;

typedef struct NodePosition NodePosition;
typedef struct EdgePath EdgePath;
typedef struct SubgraphPosition SubgraphPosition;
typedef struct LayoutNode LayoutNode;
typedef struct LayoutEdge LayoutEdge;
typedef struct LayoutLayer LayoutLayer;
typedef struct LayoutSubgraph LayoutSubgraph;

typedef lam::ArrayOwnedList<NodePosition, lam::LayoutSessionDomain> NodePositionList;
typedef lam::ArrayOwnedList<EdgePath, lam::LayoutSessionDomain> EdgePathList;
typedef lam::ArrayOwnedList<SubgraphPosition, lam::LayoutSessionDomain> SubgraphPositionList;
typedef lam::ArrayOwnedList<LayoutNode, lam::LayoutSessionDomain> LayoutNodeList;
typedef lam::ArrayOwnedList<LayoutEdge, lam::LayoutSessionDomain> LayoutEdgeList;
typedef lam::ArrayOwnedList<LayoutLayer, lam::LayoutSessionDomain> LayoutLayerList;
typedef lam::ArrayOwnedList<LayoutSubgraph, lam::LayoutSessionDomain> LayoutSubgraphList;
typedef lam::ArrayList<LayoutNode*> LayoutNodeRefList;
typedef lam::ArrayList<LayoutEdge*> LayoutEdgeRefList;
typedef lam::ArrayList<LayoutSubgraph*> LayoutSubgraphRefList;
typedef lam::ArrayList<const char*> GraphNodeIdList;

typedef lam::PersistentList<NodePositionList, lam::LayoutSessionDomain> PersistentNodePositionList;
typedef lam::PersistentList<EdgePathList, lam::LayoutSessionDomain> PersistentEdgePathList;
typedef lam::PersistentList<SubgraphPositionList, lam::LayoutSessionDomain> PersistentSubgraphPositionList;
typedef lam::PersistentList<LayoutNodeList, lam::LayoutSessionDomain> PersistentLayoutNodeList;
typedef lam::PersistentList<LayoutEdgeList, lam::LayoutSessionDomain> PersistentLayoutEdgeList;
typedef lam::PersistentList<LayoutLayerList, lam::LayoutSessionDomain> PersistentLayoutLayerList;
typedef lam::PersistentList<LayoutSubgraphList, lam::LayoutSessionDomain> PersistentLayoutSubgraphList;

template<typename List>
static inline List* graph_list_new(size_t initial_capacity) {
    void* raw = mem_alloc(sizeof(List), MEM_CAT_LAYOUT);
    if (!raw) return nullptr;
    return new (raw) List(MEM_CAT_LAYOUT, initial_capacity); // NEW_DELETE_OK: single audited boundary for List construction inside graph_list_new factory.
}

template<typename List>
static inline void graph_list_free(List* list) {
    if (!list) return;
    list->~List();
    mem_free(list);
}

// 2D point for coordinates and paths
typedef struct Point2D {
    float x;
    float y;
} Point2D;

typedef lam::ArrayOwnedList<Point2D, lam::LayoutSessionDomain> Point2DList;
typedef lam::PersistentList<Point2DList, lam::LayoutSessionDomain> PersistentPoint2DList;

static inline PersistentPoint2DList* point2d_list_new(size_t initial_capacity) {
    void* raw = mem_alloc(sizeof(PersistentPoint2DList), MEM_CAT_LAYOUT);
    if (!raw) return nullptr;
    return new (raw) PersistentPoint2DList(MEM_CAT_LAYOUT, initial_capacity); // NEW_DELETE_OK: single audited boundary for PersistentPoint2DList construction inside point2d_list_new factory.
}

static inline void point2d_list_free(PersistentPoint2DList* list) {
    if (!list) return;
    list->~PersistentPoint2DList();
    mem_free(list);
}

static inline bool point2d_list_append(PersistentPoint2DList* list, const Point2D& point) {
    if (!list) return false;
    lam::SessionPtr<Point2D> owned = lam::session_make<Point2D>(MEM_CAT_LAYOUT);
    if (!owned) {
        log_error("point2d_list_append_alloc_failed");
        return false;
    }
    *owned = point;
    return list->get().append(static_cast<lam::SessionPtr<Point2D>&&>(owned));
}

static inline Point2D* point2d_list_at(PersistentPoint2DList* list, size_t index) {
    return list ? list->get()[index].get() : nullptr;
}

static inline const Point2D* point2d_list_at(const PersistentPoint2DList* list, size_t index) {
    return list ? list->get()[index].get() : nullptr;
}

static inline size_t point2d_list_size(const PersistentPoint2DList* list) {
    return list ? list->get().size() : 0;
}

// Node position after layout
typedef struct NodePosition {
    const char* node_id;    // Node identifier
    float x, y;             // Center position
    float width, height;    // Bounding box dimensions
    int rank;               // Layer index (vertical level)
    int order;              // Position within layer (horizontal)
} NodePosition;

// Edge path with control points
typedef struct EdgePath {
    const char* from_id;    // Source node ID
    const char* to_id;      // Target node ID
    PersistentPoint2DList* points; // Owned Point2D path points
    bool is_bezier;         // True for spline curves
    bool directed;          // True for directed edge (arrow at end)
    const char* edge_style; // Edge style: "solid", "dotted", "thick"
    bool arrow_start;       // True to draw arrow at start
    bool arrow_end;         // True to draw arrow at end
} EdgePath;

// Subgraph/cluster position after layout
typedef struct SubgraphPosition {
    const char* subgraph_id;    // Subgraph identifier
    const char* label;          // Display label
    float x, y;                 // Top-left position
    float width, height;        // Bounding box dimensions
    float label_height;         // Height reserved for label
} SubgraphPosition;

// Complete graph layout result
typedef struct GraphLayout {
    float graph_width;      // Total graph width
    float graph_height;     // Total graph height

    PersistentNodePositionList* node_positions;      // Owned NodePosition results
    PersistentEdgePathList* edge_paths;              // Owned EdgePath results
    PersistentSubgraphPositionList* subgraph_positions; // Owned SubgraphPosition results

    // Layout parameters
    float node_spacing_x;   // Horizontal spacing between nodes
    float node_spacing_y;   // Vertical spacing between layers (rank separation)
    float edge_spacing;     // Spacing between parallel edges

    // Metadata
    const char* algorithm;  // Layout algorithm used
    const char* direction;  // TB, LR, BT, RL
} GraphLayout;

// Layout options
typedef struct GraphLayoutOptions {
    const char* algorithm;   // "dagre", "dot", "neato"
    const char* direction;   // "TB" (top-bottom), "LR" (left-right), "BT", "RL"
    float node_sep;          // Horizontal spacing (default: 50)
    float rank_sep;          // Vertical spacing (default: 50)
    float edge_sep;          // Edge spacing (default: 10)
    bool use_splines;        // Enable curved edges (default: false)
    int max_iterations;      // For iterative algorithms (default: 100)
} GraphLayoutOptions;

// SVG generation options
typedef struct SvgGeneratorOptions {
    float canvas_padding;        // Padding around graph (default: 20)
    const char* default_fill;    // Default node fill color
    const char* default_stroke;  // Default edge stroke color
    float default_stroke_width;  // Default line width (default: 2)
    const char* font_family;     // Font for labels
    float font_size;             // Default font size (default: 14)
    bool include_grid;           // Draw background grid (default: false)

    // Theme support (optional - if set, overrides default colors)
    const struct DiagramTheme* theme;  // Color theme (NULL = use default colors)
} SvgGeneratorOptions;

// Internal: Node in layout graph (extends Element data)
typedef struct LayoutNode {
    const char* id;
    const char* label;
    const char* shape;      // "box", "circle", "ellipse", "diamond", etc.

    // Dimensions
    float width;
    float height;

    // Layout computed values
    float x, y;             // Absolute position
    int rank;               // Layer index
    int order;              // Position within layer

    // Algorithm internals
    LayoutEdgeRefList* in_edges;    // Borrowed incoming edges
    LayoutEdgeRefList* out_edges;   // Borrowed outgoing edges

    // Styling (simplified)
    const char* fill;
    const char* stroke;
} LayoutNode;

// Internal: Edge in layout graph
typedef struct LayoutEdge {
    const char* from_id;
    const char* to_id;
    const char* label;

    LayoutNode* from_node;
    LayoutNode* to_node;

    bool directed;
    bool is_back_edge;      // true if this edge creates a cycle (points backwards)
    bool arrow_start;       // true to draw arrow at start (for bidirectional)
    bool arrow_end;         // true to draw arrow at end

    // Layout computed values
    PersistentPoint2DList* path_points; // Owned Point2D path points

    // Attributes - simplified, don't need hashmap
    const char* style;      // "solid", "dotted", "thick"
} LayoutEdge;

// Internal: Layer in hierarchical layout
typedef struct LayoutLayer {
    int rank;               // Layer index
    LayoutNodeRefList* nodes; // Borrowed LayoutNode refs
} LayoutLayer;

// Internal: Subgraph/cluster in layout graph
typedef struct LayoutSubgraph {
    const char* id;         // Subgraph identifier
    const char* label;      // Display label
    const char* direction;  // Direction override ("TB", "LR", "BT", "RL", or NULL for inherit)

    GraphNodeIdList* node_ids;          // Borrowed node ID strings
    LayoutSubgraphRefList* subgraphs;   // Borrowed nested subgraphs

    // Layout computed values
    float x, y;             // Top-left position
    float width, height;    // Bounding box including padding

    // Styling
    const char* fill;       // Background fill color
    const char* stroke;     // Border stroke color
    float padding;          // Internal padding (default: 10)
    float label_height;     // Height reserved for label (default: 20)
} LayoutSubgraph;

// Internal: Graph structure for layout algorithms
typedef struct LayoutGraph {
    PersistentLayoutNodeList* nodes;        // Owned LayoutNode objects
    PersistentLayoutEdgeList* edges;        // Owned LayoutEdge objects
    PersistentLayoutLayerList* layers;      // Owned LayoutLayer objects
    PersistentLayoutSubgraphList* subgraphs; // Owned LayoutSubgraph objects

    // Graph properties
    bool is_directed;
    const char* type;       // "directed", "undirected"

    // Layout state
    float min_x, min_y;
    float max_x, max_y;
} LayoutGraph;

#endif // RADIANT_GRAPH_LAYOUT_TYPES_HPP
