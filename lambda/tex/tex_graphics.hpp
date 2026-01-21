// tex_graphics.hpp - Graphics Intermediate Representation for LaTeX
//
// Unified representation for LaTeX graphics: picture environment, pict2e, TikZ/PGF.
// All graphics are converted to this IR, then output to SVG (for HTML) or
// rendered directly (for PDF/PNG via Radiant).
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md

#ifndef TEX_GRAPHICS_HPP
#define TEX_GRAPHICS_HPP

#include "lib/arena.h"
#include "lib/strbuf.h"
#include <cstdint>
#include <cmath>

namespace tex {

// Forward declaration
struct DocElement;

// ============================================================================
// Graphics Primitive Types
// ============================================================================

enum class GraphicsType : uint8_t {
    CANVAS,     // Picture/TikZ container
    GROUP,      // Group with transform
    LINE,       // Line segment or polyline
    CIRCLE,     // Circle (filled or stroked)
    ELLIPSE,    // Ellipse
    RECT,       // Rectangle
    PATH,       // SVG-style path (M, L, C, Z commands)
    BEZIER,     // Quadratic or cubic Bezier
    POLYGON,    // Closed polygon
    ARC,        // Circular arc
    TEXT,       // Text node
    IMAGE,      // External image
};

// Get string name for debugging
const char* graphics_type_name(GraphicsType type);

// ============================================================================
// 2D Point
// ============================================================================

struct Point2D {
    float x;
    float y;
    
    Point2D() : x(0), y(0) {}
    Point2D(float x_, float y_) : x(x_), y(y_) {}
    
    Point2D operator+(const Point2D& o) const { return Point2D(x + o.x, y + o.y); }
    Point2D operator-(const Point2D& o) const { return Point2D(x - o.x, y - o.y); }
    Point2D operator*(float s) const { return Point2D(x * s, y * s); }
    
    float magnitude() const { return sqrtf(x * x + y * y); }
    Point2D normalized() const {
        float m = magnitude();
        return m > 0 ? Point2D(x / m, y / m) : Point2D(0, 0);
    }
};

// ============================================================================
// 2D Affine Transform
// ============================================================================

// 2D affine transform matrix: [a b e]
//                             [c d f]
//                             [0 0 1]
// Applied as: x' = a*x + b*y + e
//             y' = c*x + d*y + f
struct Transform2D {
    float a, b, c, d, e, f;
    
    // Identity transform
    static Transform2D identity() {
        return {1, 0, 0, 1, 0, 0};
    }
    
    // Translation
    static Transform2D translate(float tx, float ty) {
        return {1, 0, 0, 1, tx, ty};
    }
    
    // Uniform scale
    static Transform2D scale(float sx, float sy) {
        return {sx, 0, 0, sy, 0, 0};
    }
    
    // Rotation (angle in degrees)
    static Transform2D rotate(float degrees) {
        float rad = degrees * 3.14159265f / 180.0f;
        float c = cosf(rad);
        float s = sinf(rad);
        return {c, -s, s, c, 0, 0};
    }
    
    // Rotation around a point
    static Transform2D rotate_around(float degrees, float cx, float cy) {
        return translate(cx, cy).multiply(rotate(degrees)).multiply(translate(-cx, -cy));
    }
    
    // Matrix multiplication: this * other
    Transform2D multiply(const Transform2D& o) const {
        return {
            a * o.a + b * o.c,
            a * o.b + b * o.d,
            c * o.a + d * o.c,
            c * o.b + d * o.d,
            a * o.e + b * o.f + e,
            c * o.e + d * o.f + f
        };
    }
    
    // Apply transform to a point
    Point2D apply(const Point2D& p) const {
        return Point2D(a * p.x + b * p.y + e, c * p.x + d * p.y + f);
    }
    
    // Check if this is identity (or close to it)
    bool is_identity() const {
        const float eps = 1e-6f;
        return fabsf(a - 1) < eps && fabsf(b) < eps &&
               fabsf(c) < eps && fabsf(d - 1) < eps &&
               fabsf(e) < eps && fabsf(f) < eps;
    }
};

// ============================================================================
// Graphics Style
// ============================================================================

struct GraphicsStyle {
    const char* stroke_color;      // e.g., "#000000", "none", "currentColor"
    const char* fill_color;        // e.g., "#FF0000", "none"
    float stroke_width;            // Line width in pt (0 = default/inherit)
    const char* stroke_dasharray;  // e.g., "5,3" for dashed, nullptr = solid
    const char* stroke_linecap;    // "butt", "round", "square", nullptr = default
    const char* stroke_linejoin;   // "miter", "round", "bevel", nullptr = default
    float miter_limit;             // Miter limit (0 = default)
    float opacity;                 // 0-1, 0 = inherit
    
    // Arrow/marker specifications
    const char* marker_start;      // URL or predefined marker
    const char* marker_mid;
    const char* marker_end;
    
    // Default style
    static GraphicsStyle defaults() {
        GraphicsStyle s = {};
        s.stroke_color = "#000000";
        s.fill_color = "none";
        s.stroke_width = 0.4f;      // LaTeX default thin line
        s.stroke_dasharray = nullptr;
        s.stroke_linecap = nullptr;
        s.stroke_linejoin = nullptr;
        s.miter_limit = 0;
        s.opacity = 0;
        s.marker_start = nullptr;
        s.marker_mid = nullptr;
        s.marker_end = nullptr;
        return s;
    }
    
    // No stroke
    static GraphicsStyle no_stroke() {
        GraphicsStyle s = defaults();
        s.stroke_color = "none";
        return s;
    }
    
    // Filled shape
    static GraphicsStyle filled(const char* color = "#000000") {
        GraphicsStyle s = defaults();
        s.stroke_color = "none";
        s.fill_color = color;
        return s;
    }
};

// ============================================================================
// Graphics Element
// ============================================================================

struct GraphicsElement {
    GraphicsType type;
    GraphicsStyle style;
    Transform2D transform;
    
    // Linked list pointers
    GraphicsElement* next;         // Next sibling
    GraphicsElement* children;     // First child (for GROUP/CANVAS)
    
    // Type-specific data (union)
    union {
        // CANVAS - picture/tikzpicture container
        struct {
            float width;           // Canvas width in pt
            float height;          // Canvas height in pt
            float origin_x;        // Origin offset (picture environment)
            float origin_y;
            float unitlength;      // Scale factor (default 1pt)
            bool flip_y;           // Flip Y axis (LaTeX uses bottom-up)
        } canvas;
        
        // LINE - line segment or polyline
        struct {
            Point2D* points;       // Array of points
            int point_count;
            bool has_arrow;        // Arrow at end
            bool has_arrow_start;  // Arrow at start
        } line;
        
        // CIRCLE
        struct {
            Point2D center;
            float radius;
            bool filled;
        } circle;
        
        // ELLIPSE
        struct {
            Point2D center;
            float rx;              // X radius
            float ry;              // Y radius
        } ellipse;
        
        // RECT
        struct {
            Point2D corner;        // Top-left corner
            float width;
            float height;
            float rx, ry;          // Rounded corner radii
        } rect;
        
        // PATH - SVG path data
        struct {
            const char* d;         // SVG path data string (M, L, C, Z commands)
        } path;
        
        // BEZIER - quadratic or cubic
        struct {
            Point2D p0;            // Start point
            Point2D p1;            // Control point 1
            Point2D p2;            // Control point 2 (cubic) or end (quadratic)
            Point2D p3;            // End point (cubic only)
            bool is_quadratic;     // true = quadratic (p3 unused)
        } bezier;
        
        // POLYGON
        struct {
            Point2D* points;
            int point_count;
            bool closed;           // true = close path
        } polygon;
        
        // ARC
        struct {
            Point2D center;
            float radius;
            float start_angle;     // In degrees
            float end_angle;
            bool filled;
        } arc;
        
        // TEXT - text node (for TikZ nodes)
        struct {
            Point2D pos;
            const char* text;           // Plain text content
            const char* anchor;         // "start", "middle", "end"
            const char* baseline;       // "auto", "middle", "hanging"
            DocElement* rich_content;   // For formatted content (math, etc.)
            float font_size;            // 0 = inherit
        } text;
        
        // IMAGE
        struct {
            Point2D pos;
            float width;
            float height;
            const char* src;
        } image;
        
        // GROUP - no extra data, just children
        struct {
            const char* id;        // Optional ID for clipping, etc.
            const char* clip_path; // Clip path reference
        } group;
    };
};

// ============================================================================
// Graphics Element Allocation
// ============================================================================

// Allocate a new graphics element from arena
GraphicsElement* graphics_alloc(Arena* arena, GraphicsType type);

// Allocate point array from arena
Point2D* graphics_alloc_points(Arena* arena, int count);

// ============================================================================
// Graphics Element Builders
// ============================================================================

// Create canvas (picture/tikzpicture container)
GraphicsElement* graphics_canvas(Arena* arena, float width, float height,
                                  float origin_x = 0, float origin_y = 0,
                                  float unitlength = 1.0f);

// Create a group
GraphicsElement* graphics_group(Arena* arena, const Transform2D* transform = nullptr);

// Create a line from two points
GraphicsElement* graphics_line(Arena* arena, float x1, float y1, float x2, float y2);

// Create a polyline from points
GraphicsElement* graphics_polyline(Arena* arena, const Point2D* points, int count);

// Create a circle
GraphicsElement* graphics_circle(Arena* arena, float cx, float cy, float r, bool filled = false);

// Create an ellipse
GraphicsElement* graphics_ellipse(Arena* arena, float cx, float cy, float rx, float ry);

// Create a rectangle
GraphicsElement* graphics_rect(Arena* arena, float x, float y, float w, float h,
                                float rx = 0, float ry = 0);

// Create an SVG path
GraphicsElement* graphics_path(Arena* arena, const char* path_data);

// Create a quadratic Bezier
GraphicsElement* graphics_qbezier(Arena* arena, 
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2);

// Create a cubic Bezier
GraphicsElement* graphics_cbezier(Arena* arena,
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2,
                                   float x3, float y3);

// Create a text node
GraphicsElement* graphics_text(Arena* arena, float x, float y, const char* text);

// Create an arc
GraphicsElement* graphics_arc(Arena* arena, float cx, float cy, float r,
                               float start_deg, float end_deg, bool filled = false);

// ============================================================================
// Graphics Tree Operations
// ============================================================================

// Append child to parent (CANVAS or GROUP)
void graphics_append_child(GraphicsElement* parent, GraphicsElement* child);

// Append sibling after element
void graphics_append_sibling(GraphicsElement* elem, GraphicsElement* sibling);

// Calculate bounding box of element tree
struct BoundingBox {
    float min_x, min_y, max_x, max_y;
    
    float width() const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    bool is_empty() const { return min_x > max_x; }
    
    static BoundingBox empty() {
        return {1e9f, 1e9f, -1e9f, -1e9f};
    }
    
    void include(float x, float y) {
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    
    void include(const BoundingBox& other) {
        if (other.min_x < min_x) min_x = other.min_x;
        if (other.max_x > max_x) max_x = other.max_x;
        if (other.min_y < min_y) min_y = other.min_y;
        if (other.max_y > max_y) max_y = other.max_y;
    }
};

BoundingBox graphics_bounding_box(const GraphicsElement* root);

// ============================================================================
// SVG Output
// ============================================================================

// Convert graphics element tree to SVG string
void graphics_to_svg(const GraphicsElement* root, StrBuf* out);

// Convert graphics element tree to inline SVG for HTML embedding
void graphics_to_inline_svg(const GraphicsElement* root, StrBuf* out);

// ============================================================================
// Standard Arrow Marker Definitions
// ============================================================================

// Emit standard arrow marker definitions (to be included in <defs>)
void graphics_emit_arrow_defs(StrBuf* out);

} // namespace tex

#endif // TEX_GRAPHICS_HPP
