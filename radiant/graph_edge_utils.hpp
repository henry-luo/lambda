#ifndef RADIANT_GRAPH_EDGE_UTILS_HPP
#define RADIANT_GRAPH_EDGE_UTILS_HPP

#include "graph_layout_types.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert diagonal edge segments to orthogonal (90-degree) bends.
 *
 * Dagre may produce edges with diagonal segments. This function converts
 * them into L-shaped orthogonal paths for a cleaner, more professional look.
 *
 * @param points Array of Point2D* - modified in place
 * @param vertical_first If true, prefer vertical-then-horizontal bends (for TB/BT layouts)
 *                       If false, prefer horizontal-then-vertical bends (for LR/RL layouts)
 */
void snap_to_orthogonal(ArrayList* points, bool vertical_first);

/**
 * Clip edge endpoint to diamond shape boundary.
 *
 * Dagre treats all nodes as rectangles, so edge endpoints land on the
 * rectangle boundary. For diamond shapes, the visual boundary is inscribed
 * within the rectangle. This function projects the endpoint onto the diamond.
 *
 * Diamond boundary equation: |dx|/hw + |dy|/hh = 1
 *
 * @param point The point to clip
 * @param cx, cy Center of the diamond
 * @param hw, hh Half-width and half-height
 * @return Clipped point on the diamond boundary
 */
Point2D clip_to_diamond_boundary(Point2D point, float cx, float cy, float hw, float hh);

/**
 * Clip edge endpoint to circle boundary.
 *
 * Similar to diamond clipping - projects endpoint onto the circle inscribed
 * within the rectangular bounding box.
 *
 * @param point The point to clip
 * @param cx, cy Center of the circle
 * @param r Radius of the circle
 * @return Clipped point on the circle boundary
 */
Point2D clip_to_circle_boundary(Point2D point, float cx, float cy, float r);

/**
 * Clip edge endpoint to ellipse boundary.
 *
 * @param point The point to clip
 * @param cx, cy Center of the ellipse
 * @param rx, ry Horizontal and vertical radii
 * @return Clipped point on the ellipse boundary
 */
Point2D clip_to_ellipse_boundary(Point2D point, float cx, float cy, float rx, float ry);

/**
 * Clip edge endpoint to stadium (pill) shape boundary.
 *
 * A stadium is a rectangle with semicircular ends.
 *
 * @param point The point to clip
 * @param cx, cy Center of the stadium
 * @param hw, hh Half-width and half-height
 * @return Clipped point on the stadium boundary
 */
Point2D clip_to_stadium_boundary(Point2D point, float cx, float cy, float hw, float hh);

/**
 * Clip edge endpoint to hexagon boundary.
 *
 * @param point The point to clip
 * @param cx, cy Center of the hexagon
 * @param r Radius (distance from center to vertex)
 * @return Clipped point on the hexagon boundary
 */
Point2D clip_to_hexagon_boundary(Point2D point, float cx, float cy, float r);

/**
 * Remove collinear intermediate points from a polyline.
 *
 * If three consecutive points lie on the same horizontal or vertical line,
 * the middle point is redundant and can cause visual artifacts.
 *
 * @param points Array of Point2D* - modified in place
 */
void remove_collinear_points(ArrayList* points);

/**
 * Apply all edge post-processing steps based on node shapes.
 *
 * This is the main entry point that combines orthogonal snapping,
 * shape-specific clipping, and collinear point removal.
 *
 * @param graph The layout graph with positioned nodes and edges
 * @param direction Layout direction ("TB", "LR", "BT", "RL")
 */
void post_process_edges(LayoutGraph* graph, const char* direction);

/**
 * Get the appropriate clipping function for a shape type.
 *
 * @param shape Shape name ("circle", "diamond", "ellipse", etc.)
 * @return true if shape requires special boundary clipping
 */
bool shape_needs_special_clipping(const char* shape);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_GRAPH_EDGE_UTILS_HPP
