#ifndef RADIANT_GRAPH_LAYOUT_TYPES_HPP
#define RADIANT_GRAPH_LAYOUT_TYPES_HPP

#include "../lib/hashmap.h"
#include "../lib/arraylist.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2D point for coordinates and paths
typedef struct Point2D {
    float x;
    float y;
} Point2D;

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
    ArrayList* points;      // Array of Point2D
    bool is_bezier;         // True for spline curves
    bool directed;          // True for directed edge (arrow at end)
    const char* edge_style; // Edge style: "solid", "dotted", "thick"
    bool arrow_start;       // True to draw arrow at start
    bool arrow_end;         // True to draw arrow at end
} EdgePath;

// Complete graph layout result
typedef struct GraphLayout {
    float graph_width;      // Total graph width
    float graph_height;     // Total graph height

    ArrayList* node_positions; // Array of NodePosition*
    ArrayList* edge_paths;   // Array of EdgePath*

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
    ArrayList* in_edges;    // Incoming edges
    ArrayList* out_edges;   // Outgoing edges

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
    ArrayList* path_points;  // Array of Point2D

    // Attributes - simplified, don't need hashmap
    const char* style;      // "solid", "dotted", "thick"
} LayoutEdge;

// Internal: Layer in hierarchical layout
typedef struct LayoutLayer {
    int rank;               // Layer index
    ArrayList* nodes;       // Array of LayoutNode*
} LayoutLayer;

// Internal: Graph structure for layout algorithms
typedef struct LayoutGraph {
    ArrayList* nodes;       // Array of LayoutNode*
    ArrayList* edges;       // Array of LayoutEdge*
    ArrayList* layers;      // Array of LayoutLayer* (for hierarchical layouts)

    // Graph properties
    bool is_directed;
    const char* type;       // "directed", "undirected"

    // Layout state
    float min_x, min_y;
    float max_x, max_y;
} LayoutGraph;

#ifdef __cplusplus
}
#endif

#endif // RADIANT_GRAPH_LAYOUT_TYPES_HPP
