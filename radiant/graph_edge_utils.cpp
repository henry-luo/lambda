#include "graph_edge_utils.hpp"
#include "../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============================================================================
// Orthogonal Snapping
// ============================================================================

void snap_to_orthogonal(ArrayList* points, bool vertical_first) {
    if (!points || points->length < 2) return;

    ArrayList* result = arraylist_new(points->length * 2);

    // copy first point
    Point2D* first = (Point2D*)points->data[0];
    Point2D* first_copy = (Point2D*)calloc(1, sizeof(Point2D));
    first_copy->x = first->x;
    first_copy->y = first->y;
    arraylist_append(result, first_copy);

    for (int i = 1; i < points->length; i++) {
        Point2D* prev = (Point2D*)result->data[result->length - 1];
        Point2D* curr = (Point2D*)points->data[i];

        float dx = fabsf(curr->x - prev->x);
        float dy = fabsf(curr->y - prev->y);

        // Already axis-aligned (or close enough)
        if (dx < 1.0f || dy < 1.0f) {
            Point2D* curr_copy = (Point2D*)calloc(1, sizeof(Point2D));
            curr_copy->x = curr->x;
            curr_copy->y = curr->y;
            arraylist_append(result, curr_copy);
            continue;
        }

        // Insert L-bend
        // TD/BT layouts: vertical first — edge drops along the rank axis, then adjusts
        // LR/RL layouts: horizontal first — edge moves along the rank axis, then adjusts
        Point2D* bend = (Point2D*)calloc(1, sizeof(Point2D));
        if (vertical_first) {
            bend->x = prev->x;
            bend->y = curr->y;
        } else {
            bend->x = curr->x;
            bend->y = prev->y;
        }
        arraylist_append(result, bend);

        Point2D* curr_copy = (Point2D*)calloc(1, sizeof(Point2D));
        curr_copy->x = curr->x;
        curr_copy->y = curr->y;
        arraylist_append(result, curr_copy);
    }

    // Replace original points with result
    for (int i = 0; i < points->length; i++) {
        free(points->data[i]);
    }
    arraylist_clear(points);

    for (int i = 0; i < result->length; i++) {
        arraylist_append(points, result->data[i]);
    }
    arraylist_free(result);

    // Remove collinear points after orthogonalization
    remove_collinear_points(points);
}

// ============================================================================
// Collinear Point Removal
// ============================================================================

void remove_collinear_points(ArrayList* points) {
    if (!points || points->length < 3) return;

    ArrayList* result = arraylist_new(points->length);

    // Always keep first point
    arraylist_append(result, points->data[0]);

    for (int i = 1; i < points->length - 1; i++) {
        Point2D* a = (Point2D*)result->data[result->length - 1];
        Point2D* b = (Point2D*)points->data[i];
        Point2D* c = (Point2D*)points->data[i + 1];

        // Check if a-b-c are collinear (on same horizontal or vertical line)
        bool same_x = fabsf(a->x - b->x) < 1.0f && fabsf(b->x - c->x) < 1.0f;
        bool same_y = fabsf(a->y - b->y) < 1.0f && fabsf(b->y - c->y) < 1.0f;

        if (same_x || same_y) {
            // Skip redundant middle point - free it
            free(points->data[i]);
            continue;
        }

        arraylist_append(result, points->data[i]);
    }

    // Always keep last point
    arraylist_append(result, points->data[points->length - 1]);

    // Replace original array
    arraylist_clear(points);
    for (int i = 0; i < result->length; i++) {
        arraylist_append(points, result->data[i]);
    }
    arraylist_free(result);
}

// ============================================================================
// Shape Boundary Clipping
// ============================================================================

Point2D clip_to_diamond_boundary(Point2D point, float cx, float cy, float hw, float hh) {
    float dx = point.x - cx;
    float dy = point.y - cy;

    // Point at or very near center
    if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f) {
        return point;
    }

    // Diamond boundary satisfies: |dx|/hw + |dy|/hh = 1
    // Scale the direction vector to land on this boundary
    float scale = 1.0f / (fabsf(dx) / hw + fabsf(dy) / hh);

    Point2D result;
    result.x = cx + scale * dx;
    result.y = cy + scale * dy;
    return result;
}

Point2D clip_to_circle_boundary(Point2D point, float cx, float cy, float r) {
    float dx = point.x - cx;
    float dy = point.y - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    // Point at or very near center
    if (dist < 0.5f) {
        return point;
    }

    float scale = r / dist;

    Point2D result;
    result.x = cx + scale * dx;
    result.y = cy + scale * dy;
    return result;
}

Point2D clip_to_ellipse_boundary(Point2D point, float cx, float cy, float rx, float ry) {
    float dx = point.x - cx;
    float dy = point.y - cy;

    // Point at or very near center
    if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f) {
        return point;
    }

    // Ellipse boundary satisfies: (dx/rx)² + (dy/ry)² = 1
    // Normalize direction and find intersection
    float dist = sqrtf(dx * dx + dy * dy);
    float norm_x = dx / dist;
    float norm_y = dy / dist;

    // Parametric: x = t*norm_x, y = t*norm_y
    // Substitute: (t*norm_x/rx)² + (t*norm_y/ry)² = 1
    // t² * (norm_x²/rx² + norm_y²/ry²) = 1
    float denom = (norm_x * norm_x) / (rx * rx) + (norm_y * norm_y) / (ry * ry);
    float t = sqrtf(1.0f / denom);

    Point2D result;
    result.x = cx + t * norm_x;
    result.y = cy + t * norm_y;
    return result;
}

Point2D clip_to_stadium_boundary(Point2D point, float cx, float cy, float hw, float hh) {
    float dx = point.x - cx;
    float dy = point.y - cy;

    // Stadium = rectangle with semicircular ends
    // Determine if we hit the rectangular part or the circular caps
    float cap_radius = fminf(hw, hh);
    float rect_extent = fabsf(hw > hh ? hw - cap_radius : hh - cap_radius);

    Point2D result;

    if (hw > hh) {
        // Horizontal stadium (caps on left/right)
        if (fabsf(dx) > rect_extent) {
            // Hitting a semicircular cap
            float cap_cx = (dx > 0) ? cx + rect_extent : cx - rect_extent;
            result = clip_to_circle_boundary(point, cap_cx, cy, cap_radius);
        } else {
            // Hitting the rectangular part (top or bottom edge)
            result.x = point.x;
            result.y = (dy > 0) ? cy + hh : cy - hh;
        }
    } else {
        // Vertical stadium (caps on top/bottom)
        if (fabsf(dy) > rect_extent) {
            // Hitting a semicircular cap
            float cap_cy = (dy > 0) ? cy + rect_extent : cy - rect_extent;
            result = clip_to_circle_boundary(point, cx, cap_cy, cap_radius);
        } else {
            // Hitting the rectangular part (left or right edge)
            result.x = (dx > 0) ? cx + hw : cx - hw;
            result.y = point.y;
        }
    }

    return result;
}

Point2D clip_to_hexagon_boundary(Point2D point, float cx, float cy, float r) {
    float dx = point.x - cx;
    float dy = point.y - cy;

    // Point at or very near center
    if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f) {
        return point;
    }

    // Regular hexagon with vertices at angles 0°, 60°, 120°, 180°, 240°, 300°
    // Each edge connects two adjacent vertices
    // Find which edge the ray from center intersects

    float angle = atan2f(dy, dx);
    if (angle < 0) angle += 2.0f * M_PI;

    // Determine which edge (0-5) we're hitting
    int edge_idx = (int)(angle / (M_PI / 3.0f)) % 6;

    // Vertices of the hexagon
    float v1_angle = edge_idx * M_PI / 3.0f;
    float v2_angle = (edge_idx + 1) * M_PI / 3.0f;

    float v1_x = cx + r * cosf(v1_angle);
    float v1_y = cy + r * sinf(v1_angle);
    float v2_x = cx + r * cosf(v2_angle);
    float v2_y = cy + r * sinf(v2_angle);

    // Find intersection of ray from center through point with edge v1-v2
    // Edge direction
    float ex = v2_x - v1_x;
    float ey = v2_y - v1_y;

    // Ray direction (center to point)
    float rx = dx;
    float ry = dy;

    // Solve: v1 + t * e = s * r for t in [0,1]
    float cross = rx * ey - ry * ex;
    if (fabsf(cross) < 0.001f) {
        // Ray parallel to edge - just return point on circle
        return clip_to_circle_boundary(point, cx, cy, r);
    }

    // Distance from center to edge along ray
    float t = ((v1_x - cx) * ry - (v1_y - cy) * rx) / (-cross);

    Point2D result;
    result.x = v1_x + t * ex;
    result.y = v1_y + t * ey;
    return result;
}

// ============================================================================
// Shape Classification
// ============================================================================

bool shape_needs_special_clipping(const char* shape) {
    if (!shape) return false;

    // These shapes are inscribed within their rectangular bounding box
    // and need special clipping
    return strcmp(shape, "circle") == 0 ||
           strcmp(shape, "doublecircle") == 0 ||
           strcmp(shape, "ellipse") == 0 ||
           strcmp(shape, "diamond") == 0 ||
           strcmp(shape, "hexagon") == 0 ||
           strcmp(shape, "stadium") == 0 ||
           strcmp(shape, "cylinder") == 0 ||  // top ellipse needs clipping
           strcmp(shape, "state-start") == 0 ||
           strcmp(shape, "state-end") == 0;
}

// ============================================================================
// Helper: Clip single endpoint based on shape
// ============================================================================

static Point2D clip_endpoint_to_shape(Point2D endpoint, LayoutNode* node) {
    float cx = node->x;
    float cy = node->y;
    float hw = node->width / 2.0f;
    float hh = node->height / 2.0f;

    const char* shape = node->shape;

    if (!shape) {
        return endpoint;  // No clipping for unknown shapes
    }

    if (strcmp(shape, "circle") == 0 ||
        strcmp(shape, "doublecircle") == 0 ||
        strcmp(shape, "state-start") == 0 ||
        strcmp(shape, "state-end") == 0) {
        float r = fminf(hw, hh);
        return clip_to_circle_boundary(endpoint, cx, cy, r);
    }

    if (strcmp(shape, "ellipse") == 0) {
        return clip_to_ellipse_boundary(endpoint, cx, cy, hw, hh);
    }

    if (strcmp(shape, "diamond") == 0) {
        return clip_to_diamond_boundary(endpoint, cx, cy, hw, hh);
    }

    if (strcmp(shape, "hexagon") == 0) {
        float r = fminf(hw, hh);
        return clip_to_hexagon_boundary(endpoint, cx, cy, r);
    }

    if (strcmp(shape, "stadium") == 0) {
        return clip_to_stadium_boundary(endpoint, cx, cy, hw, hh);
    }

    if (strcmp(shape, "cylinder") == 0) {
        // Cylinder top is an ellipse - clip to ellipse for top connections
        // For simplicity, treat as ellipse
        return clip_to_ellipse_boundary(endpoint, cx, cy, hw, hh * 0.9f);
    }

    // Default: no special clipping (rectangle handled elsewhere)
    return endpoint;
}

// ============================================================================
// Main Post-Processing Entry Point
// ============================================================================

void post_process_edges(LayoutGraph* graph, const char* direction) {
    if (!graph || !graph->edges) return;

    // Determine bend direction based on layout direction
    bool vertical_first = true;  // default for TD/TB
    if (direction) {
        if (strcmp(direction, "LR") == 0 || strcmp(direction, "RL") == 0) {
            vertical_first = false;
        }
    }

    log_debug("post-processing edges: direction=%s, vertical_first=%d",
              direction ? direction : "TB", vertical_first);

    for (int i = 0; i < graph->edges->length; i++) {
        LayoutEdge* edge = (LayoutEdge*)graph->edges->data[i];

        if (!edge->path_points || edge->path_points->length < 2) {
            continue;
        }

        // Step 1: Clip endpoints to non-rectangular shapes
        LayoutNode* from_node = edge->from_node;
        LayoutNode* to_node = edge->to_node;

        if (from_node && shape_needs_special_clipping(from_node->shape)) {
            Point2D* start_pt = (Point2D*)edge->path_points->data[0];
            Point2D clipped = clip_endpoint_to_shape(*start_pt, from_node);
            start_pt->x = clipped.x;
            start_pt->y = clipped.y;
        }

        if (to_node && shape_needs_special_clipping(to_node->shape)) {
            int last_idx = edge->path_points->length - 1;
            Point2D* end_pt = (Point2D*)edge->path_points->data[last_idx];
            Point2D clipped = clip_endpoint_to_shape(*end_pt, to_node);
            end_pt->x = clipped.x;
            end_pt->y = clipped.y;
        }

        // Step 2: Snap to orthogonal
        snap_to_orthogonal(edge->path_points, vertical_first);

        // Step 3: Re-clip endpoints after orthogonalization if needed
        // The orthogonal snapping may have changed the first/last segment direction
        if (from_node && shape_needs_special_clipping(from_node->shape)) {
            Point2D* start_pt = (Point2D*)edge->path_points->data[0];
            Point2D clipped = clip_endpoint_to_shape(*start_pt, from_node);
            start_pt->x = clipped.x;
            start_pt->y = clipped.y;
        }

        if (to_node && shape_needs_special_clipping(to_node->shape)) {
            int last_idx = edge->path_points->length - 1;
            Point2D* end_pt = (Point2D*)edge->path_points->data[last_idx];
            Point2D clipped = clip_endpoint_to_shape(*end_pt, to_node);
            end_pt->x = clipped.x;
            end_pt->y = clipped.y;
        }
    }

    log_debug("edge post-processing complete");
}
