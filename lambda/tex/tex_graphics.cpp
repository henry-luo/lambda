// tex_graphics.cpp - Graphics IR implementation and SVG output
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md

#include "tex_graphics.hpp"
#include "lib/log.h"
#include <cstdio>
#include <cstring>

namespace tex {

// ============================================================================
// Debug Names
// ============================================================================

const char* graphics_type_name(GraphicsType type) {
    switch (type) {
        case GraphicsType::CANVAS:  return "CANVAS";
        case GraphicsType::GROUP:   return "GROUP";
        case GraphicsType::LINE:    return "LINE";
        case GraphicsType::CIRCLE:  return "CIRCLE";
        case GraphicsType::ELLIPSE: return "ELLIPSE";
        case GraphicsType::RECT:    return "RECT";
        case GraphicsType::PATH:    return "PATH";
        case GraphicsType::BEZIER:  return "BEZIER";
        case GraphicsType::POLYGON: return "POLYGON";
        case GraphicsType::ARC:     return "ARC";
        case GraphicsType::TEXT:    return "TEXT";
        case GraphicsType::IMAGE:   return "IMAGE";
        default:                    return "UNKNOWN";
    }
}

// ============================================================================
// Allocation
// ============================================================================

GraphicsElement* graphics_alloc(Arena* arena, GraphicsType type) {
    GraphicsElement* elem = (GraphicsElement*)arena_calloc(arena, sizeof(GraphicsElement));
    elem->type = type;
    elem->style = GraphicsStyle::defaults();
    elem->transform = Transform2D::identity();
    elem->next = nullptr;
    elem->children = nullptr;
    return elem;
}

Point2D* graphics_alloc_points(Arena* arena, int count) {
    return (Point2D*)arena_calloc(arena, count * sizeof(Point2D));
}

// ============================================================================
// Element Builders
// ============================================================================

GraphicsElement* graphics_canvas(Arena* arena, float width, float height,
                                  float origin_x, float origin_y,
                                  float unitlength) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::CANVAS);
    elem->canvas.width = width;
    elem->canvas.height = height;
    elem->canvas.origin_x = origin_x;
    elem->canvas.origin_y = origin_y;
    elem->canvas.unitlength = unitlength;
    elem->canvas.flip_y = true;  // LaTeX uses bottom-up Y axis
    return elem;
}

GraphicsElement* graphics_group(Arena* arena, const Transform2D* transform) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::GROUP);
    if (transform) {
        elem->transform = *transform;
    }
    elem->group.id = nullptr;
    elem->group.clip_path = nullptr;
    return elem;
}

GraphicsElement* graphics_line(Arena* arena, float x1, float y1, float x2, float y2) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::LINE);
    elem->line.points = graphics_alloc_points(arena, 2);
    elem->line.points[0] = Point2D(x1, y1);
    elem->line.points[1] = Point2D(x2, y2);
    elem->line.point_count = 2;
    elem->line.has_arrow = false;
    elem->line.has_arrow_start = false;
    return elem;
}

GraphicsElement* graphics_polyline(Arena* arena, const Point2D* points, int count) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::LINE);
    elem->line.points = graphics_alloc_points(arena, count);
    memcpy(elem->line.points, points, count * sizeof(Point2D));
    elem->line.point_count = count;
    elem->line.has_arrow = false;
    elem->line.has_arrow_start = false;
    return elem;
}

GraphicsElement* graphics_circle(Arena* arena, float cx, float cy, float r, bool filled) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::CIRCLE);
    elem->circle.center = Point2D(cx, cy);
    elem->circle.radius = r;
    elem->circle.filled = filled;
    if (filled) {
        elem->style.fill_color = "#000000";
        elem->style.stroke_color = "none";
    }
    return elem;
}

GraphicsElement* graphics_ellipse(Arena* arena, float cx, float cy, float rx, float ry) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::ELLIPSE);
    elem->ellipse.center = Point2D(cx, cy);
    elem->ellipse.rx = rx;
    elem->ellipse.ry = ry;
    return elem;
}

GraphicsElement* graphics_rect(Arena* arena, float x, float y, float w, float h,
                                float rx, float ry) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::RECT);
    elem->rect.corner = Point2D(x, y);
    elem->rect.width = w;
    elem->rect.height = h;
    elem->rect.rx = rx;
    elem->rect.ry = ry;
    return elem;
}

GraphicsElement* graphics_path(Arena* arena, const char* path_data) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::PATH);
    elem->path.d = path_data;
    return elem;
}

GraphicsElement* graphics_qbezier(Arena* arena,
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::BEZIER);
    elem->bezier.p0 = Point2D(x0, y0);
    elem->bezier.p1 = Point2D(x1, y1);
    elem->bezier.p2 = Point2D(x2, y2);
    elem->bezier.is_quadratic = true;
    return elem;
}

GraphicsElement* graphics_cbezier(Arena* arena,
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2,
                                   float x3, float y3) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::BEZIER);
    elem->bezier.p0 = Point2D(x0, y0);
    elem->bezier.p1 = Point2D(x1, y1);
    elem->bezier.p2 = Point2D(x2, y2);
    elem->bezier.p3 = Point2D(x3, y3);
    elem->bezier.is_quadratic = false;
    return elem;
}

GraphicsElement* graphics_text(Arena* arena, float x, float y, const char* text) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::TEXT);
    elem->text.pos = Point2D(x, y);
    elem->text.text = text;
    elem->text.anchor = "middle";
    elem->text.baseline = "middle";
    elem->text.rich_content = nullptr;
    elem->text.font_size = 0;
    return elem;
}

GraphicsElement* graphics_arc(Arena* arena, float cx, float cy, float r,
                               float start_deg, float end_deg, bool filled) {
    GraphicsElement* elem = graphics_alloc(arena, GraphicsType::ARC);
    elem->arc.center = Point2D(cx, cy);
    elem->arc.radius = r;
    elem->arc.start_angle = start_deg;
    elem->arc.end_angle = end_deg;
    elem->arc.filled = filled;
    return elem;
}

// ============================================================================
// Tree Operations
// ============================================================================

void graphics_append_child(GraphicsElement* parent, GraphicsElement* child) {
    if (!parent || !child) return;
    
    if (parent->type != GraphicsType::CANVAS && parent->type != GraphicsType::GROUP) {
        log_error("graphics_append_child: parent must be CANVAS or GROUP");
        return;
    }
    
    if (!parent->children) {
        parent->children = child;
    } else {
        // Find last child
        GraphicsElement* last = parent->children;
        while (last->next) {
            last = last->next;
        }
        last->next = child;
    }
}

void graphics_append_sibling(GraphicsElement* elem, GraphicsElement* sibling) {
    if (!elem || !sibling) return;
    
    while (elem->next) {
        elem = elem->next;
    }
    elem->next = sibling;
}

// ============================================================================
// Bounding Box Calculation
// ============================================================================

static void bbox_include_element(BoundingBox* bbox, const GraphicsElement* elem,
                                  const Transform2D& parent_transform);

BoundingBox graphics_bounding_box(const GraphicsElement* root) {
    BoundingBox bbox = BoundingBox::empty();
    if (root) {
        bbox_include_element(&bbox, root, Transform2D::identity());
    }
    return bbox;
}

static void bbox_include_element(BoundingBox* bbox, const GraphicsElement* elem,
                                  const Transform2D& parent_transform) {
    Transform2D xform = parent_transform.multiply(elem->transform);
    
    switch (elem->type) {
        case GraphicsType::CANVAS:
        case GraphicsType::GROUP:
            for (GraphicsElement* child = elem->children; child; child = child->next) {
                bbox_include_element(bbox, child, xform);
            }
            break;
            
        case GraphicsType::LINE:
            for (int i = 0; i < elem->line.point_count; i++) {
                Point2D p = xform.apply(elem->line.points[i]);
                bbox->include(p.x, p.y);
            }
            break;
            
        case GraphicsType::CIRCLE: {
            Point2D c = xform.apply(elem->circle.center);
            float r = elem->circle.radius;
            bbox->include(c.x - r, c.y - r);
            bbox->include(c.x + r, c.y + r);
            break;
        }
        
        case GraphicsType::ELLIPSE: {
            Point2D c = xform.apply(elem->ellipse.center);
            bbox->include(c.x - elem->ellipse.rx, c.y - elem->ellipse.ry);
            bbox->include(c.x + elem->ellipse.rx, c.y + elem->ellipse.ry);
            break;
        }
        
        case GraphicsType::RECT: {
            Point2D corners[4] = {
                elem->rect.corner,
                Point2D(elem->rect.corner.x + elem->rect.width, elem->rect.corner.y),
                Point2D(elem->rect.corner.x + elem->rect.width, 
                        elem->rect.corner.y + elem->rect.height),
                Point2D(elem->rect.corner.x, elem->rect.corner.y + elem->rect.height)
            };
            for (int i = 0; i < 4; i++) {
                Point2D p = xform.apply(corners[i]);
                bbox->include(p.x, p.y);
            }
            break;
        }
        
        case GraphicsType::BEZIER: {
            // Approximate: include all control points
            bbox->include(xform.apply(elem->bezier.p0).x, xform.apply(elem->bezier.p0).y);
            bbox->include(xform.apply(elem->bezier.p1).x, xform.apply(elem->bezier.p1).y);
            bbox->include(xform.apply(elem->bezier.p2).x, xform.apply(elem->bezier.p2).y);
            if (!elem->bezier.is_quadratic) {
                bbox->include(xform.apply(elem->bezier.p3).x, xform.apply(elem->bezier.p3).y);
            }
            break;
        }
        
        case GraphicsType::ARC: {
            // Approximate: include center and endpoints
            Point2D c = xform.apply(elem->arc.center);
            float r = elem->arc.radius;
            bbox->include(c.x - r, c.y - r);
            bbox->include(c.x + r, c.y + r);
            break;
        }
        
        case GraphicsType::TEXT: {
            // Text bounding box is approximate
            Point2D p = xform.apply(elem->text.pos);
            bbox->include(p.x, p.y);
            break;
        }
        
        case GraphicsType::IMAGE: {
            Point2D corners[4] = {
                elem->image.pos,
                Point2D(elem->image.pos.x + elem->image.width, elem->image.pos.y),
                Point2D(elem->image.pos.x + elem->image.width, 
                        elem->image.pos.y + elem->image.height),
                Point2D(elem->image.pos.x, elem->image.pos.y + elem->image.height)
            };
            for (int i = 0; i < 4; i++) {
                Point2D p = xform.apply(corners[i]);
                bbox->include(p.x, p.y);
            }
            break;
        }
        
        case GraphicsType::POLYGON:
            for (int i = 0; i < elem->polygon.point_count; i++) {
                Point2D p = xform.apply(elem->polygon.points[i]);
                bbox->include(p.x, p.y);
            }
            break;
            
        case GraphicsType::PATH:
            // PATH bounding box requires parsing - skip for now
            break;
    }
}

// ============================================================================
// SVG Output
// ============================================================================

static void emit_svg_element(const GraphicsElement* elem, StrBuf* out, int indent);
static void emit_svg_style(const GraphicsStyle& style, StrBuf* out);
static void emit_svg_transform(const Transform2D& xform, StrBuf* out);

void graphics_emit_arrow_defs(StrBuf* out) {
    strbuf_append_str(out, "<defs>\n");
    
    // Standard arrow marker (LaTeX style)
    strbuf_append_str(out, "  <marker id=\"arrow\" markerWidth=\"10\" markerHeight=\"10\" "
                       "refX=\"9\" refY=\"3\" orient=\"auto\" markerUnits=\"strokeWidth\">\n");
    strbuf_append_str(out, "    <path d=\"M0,0 L0,6 L9,3 z\" fill=\"currentColor\"/>\n");
    strbuf_append_str(out, "  </marker>\n");
    
    // Reversed arrow (for start)
    strbuf_append_str(out, "  <marker id=\"arrow-start\" markerWidth=\"10\" markerHeight=\"10\" "
                       "refX=\"0\" refY=\"3\" orient=\"auto\" markerUnits=\"strokeWidth\">\n");
    strbuf_append_str(out, "    <path d=\"M9,0 L9,6 L0,3 z\" fill=\"currentColor\"/>\n");
    strbuf_append_str(out, "  </marker>\n");
    
    strbuf_append_str(out, "</defs>\n");
}

void graphics_to_svg(const GraphicsElement* root, StrBuf* out) {
    if (!root) return;
    
    if (root->type != GraphicsType::CANVAS) {
        log_error("graphics_to_svg: root must be CANVAS");
        return;
    }
    
    // SVG header
    strbuf_append_format(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                       "version=\"1.1\" width=\"%.2f\" height=\"%.2f\" "
                       "overflow=\"visible\">\n",
                  root->canvas.width, root->canvas.height);
    
    // Arrow definitions
    graphics_emit_arrow_defs(out);
    
    // Transform group for Y-axis flip (LaTeX uses bottom-up)
    if (root->canvas.flip_y) {
        strbuf_append_format(out, "<g transform=\"translate(%.2f,%.2f) scale(1,-1)\">\n",
                      root->canvas.origin_x, root->canvas.height + root->canvas.origin_y);
    } else if (root->canvas.origin_x != 0 || root->canvas.origin_y != 0) {
        strbuf_append_format(out, "<g transform=\"translate(%.2f,%.2f)\">\n",
                      root->canvas.origin_x, root->canvas.origin_y);
    } else {
        strbuf_append_str(out, "<g>\n");
    }
    
    // Emit children
    for (GraphicsElement* child = root->children; child; child = child->next) {
        emit_svg_element(child, out, 2);
    }
    
    strbuf_append_str(out, "</g>\n");
    strbuf_append_str(out, "</svg>\n");
}

void graphics_to_inline_svg(const GraphicsElement* root, StrBuf* out) {
    // Same as graphics_to_svg but without standalone XML declaration
    graphics_to_svg(root, out);
}

static void emit_indent(StrBuf* out, int indent) {
    for (int i = 0; i < indent; i++) {
        strbuf_append_str(out, " ");
    }
}

static void emit_svg_style(const GraphicsStyle& style, StrBuf* out) {
    if (style.stroke_color && strcmp(style.stroke_color, "none") != 0) {
        strbuf_append_format(out, " stroke=\"%s\"", style.stroke_color);
    } else {
        strbuf_append_str(out, " stroke=\"none\"");
    }
    
    if (style.fill_color && strcmp(style.fill_color, "none") != 0) {
        strbuf_append_format(out, " fill=\"%s\"", style.fill_color);
    } else {
        strbuf_append_str(out, " fill=\"none\"");
    }
    
    if (style.stroke_width > 0) {
        strbuf_append_format(out, " stroke-width=\"%.2f\"", style.stroke_width);
    }
    
    if (style.stroke_dasharray) {
        strbuf_append_format(out, " stroke-dasharray=\"%s\"", style.stroke_dasharray);
    }
    
    if (style.stroke_linecap) {
        strbuf_append_format(out, " stroke-linecap=\"%s\"", style.stroke_linecap);
    }
    
    if (style.stroke_linejoin) {
        strbuf_append_format(out, " stroke-linejoin=\"%s\"", style.stroke_linejoin);
    }
    
    if (style.opacity > 0 && style.opacity < 1) {
        strbuf_append_format(out, " opacity=\"%.2f\"", style.opacity);
    }
}

static void emit_svg_transform(const Transform2D& xform, StrBuf* out) {
    if (xform.is_identity()) return;
    
    strbuf_append_format(out, " transform=\"matrix(%.4f,%.4f,%.4f,%.4f,%.2f,%.2f)\"",
                  xform.a, xform.c, xform.b, xform.d, xform.e, xform.f);
}

static void emit_svg_element(const GraphicsElement* elem, StrBuf* out, int indent) {
    emit_indent(out, indent);
    
    switch (elem->type) {
        case GraphicsType::GROUP: {
            strbuf_append_str(out, "<g");
            if (elem->group.id) {
                strbuf_append_format(out, " id=\"%s\"", elem->group.id);
            }
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, ">\n");
            
            for (GraphicsElement* child = elem->children; child; child = child->next) {
                emit_svg_element(child, out, indent + 2);
            }
            
            emit_indent(out, indent);
            strbuf_append_str(out, "</g>\n");
            break;
        }
        
        case GraphicsType::LINE: {
            if (elem->line.point_count == 2) {
                strbuf_append_format(out, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"",
                              elem->line.points[0].x, elem->line.points[0].y,
                              elem->line.points[1].x, elem->line.points[1].y);
            } else if (elem->line.point_count > 2) {
                strbuf_append_str(out, "<polyline points=\"");
                for (int i = 0; i < elem->line.point_count; i++) {
                    if (i > 0) strbuf_append_str(out, " ");
                    strbuf_append_format(out, "%.2f,%.2f",
                                  elem->line.points[i].x, elem->line.points[i].y);
                }
                strbuf_append_str(out, "\"");
            }
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            
            if (elem->line.has_arrow) {
                strbuf_append_str(out, " marker-end=\"url(#arrow)\"");
            }
            if (elem->line.has_arrow_start) {
                strbuf_append_str(out, " marker-start=\"url(#arrow-start)\"");
            }
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::CIRCLE: {
            strbuf_append_format(out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\"",
                          elem->circle.center.x, elem->circle.center.y,
                          elem->circle.radius);
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::ELLIPSE: {
            strbuf_append_format(out, "<ellipse cx=\"%.2f\" cy=\"%.2f\" rx=\"%.2f\" ry=\"%.2f\"",
                          elem->ellipse.center.x, elem->ellipse.center.y,
                          elem->ellipse.rx, elem->ellipse.ry);
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::RECT: {
            strbuf_append_format(out, "<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\"",
                          elem->rect.corner.x, elem->rect.corner.y,
                          elem->rect.width, elem->rect.height);
            if (elem->rect.rx > 0 || elem->rect.ry > 0) {
                strbuf_append_format(out, " rx=\"%.2f\" ry=\"%.2f\"",
                              elem->rect.rx, elem->rect.ry);
            }
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::PATH: {
            strbuf_append_format(out, "<path d=\"%s\"", elem->path.d);
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::BEZIER: {
            strbuf_append_str(out, "<path d=\"");
            strbuf_append_format(out, "M %.2f,%.2f ", elem->bezier.p0.x, elem->bezier.p0.y);
            if (elem->bezier.is_quadratic) {
                strbuf_append_format(out, "Q %.2f,%.2f %.2f,%.2f",
                              elem->bezier.p1.x, elem->bezier.p1.y,
                              elem->bezier.p2.x, elem->bezier.p2.y);
            } else {
                strbuf_append_format(out, "C %.2f,%.2f %.2f,%.2f %.2f,%.2f",
                              elem->bezier.p1.x, elem->bezier.p1.y,
                              elem->bezier.p2.x, elem->bezier.p2.y,
                              elem->bezier.p3.x, elem->bezier.p3.y);
            }
            strbuf_append_str(out, "\"");
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::POLYGON: {
            if (elem->polygon.closed) {
                strbuf_append_str(out, "<polygon points=\"");
            } else {
                strbuf_append_str(out, "<polyline points=\"");
            }
            for (int i = 0; i < elem->polygon.point_count; i++) {
                if (i > 0) strbuf_append_str(out, " ");
                strbuf_append_format(out, "%.2f,%.2f",
                              elem->polygon.points[i].x, elem->polygon.points[i].y);
            }
            strbuf_append_str(out, "\"");
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::ARC: {
            // Convert arc to SVG path
            float r = elem->arc.radius;
            float start_rad = elem->arc.start_angle * 3.14159265f / 180.0f;
            float end_rad = elem->arc.end_angle * 3.14159265f / 180.0f;
            
            float x1 = elem->arc.center.x + r * cosf(start_rad);
            float y1 = elem->arc.center.y + r * sinf(start_rad);
            float x2 = elem->arc.center.x + r * cosf(end_rad);
            float y2 = elem->arc.center.y + r * sinf(end_rad);
            
            // Large arc flag and sweep direction
            float angle_diff = elem->arc.end_angle - elem->arc.start_angle;
            int large_arc = fabsf(angle_diff) > 180 ? 1 : 0;
            int sweep = angle_diff > 0 ? 1 : 0;
            
            strbuf_append_format(out, "<path d=\"M %.2f,%.2f A %.2f,%.2f 0 %d,%d %.2f,%.2f",
                          x1, y1, r, r, large_arc, sweep, x2, y2);
            
            if (elem->arc.filled) {
                strbuf_append_format(out, " L %.2f,%.2f Z",
                              elem->arc.center.x, elem->arc.center.y);
            }
            strbuf_append_str(out, "\"");
            emit_svg_style(elem->style, out);
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::TEXT: {
            strbuf_append_format(out, "<text x=\"%.2f\" y=\"%.2f\"",
                          elem->text.pos.x, elem->text.pos.y);
            
            if (elem->text.anchor) {
                strbuf_append_format(out, " text-anchor=\"%s\"", elem->text.anchor);
            }
            if (elem->text.baseline) {
                strbuf_append_format(out, " dominant-baseline=\"%s\"", elem->text.baseline);
            }
            if (elem->text.font_size > 0) {
                strbuf_append_format(out, " font-size=\"%.2f\"", elem->text.font_size);
            }
            
            // For text in flipped coordinate system, we need to flip back
            strbuf_append_str(out, " transform=\"scale(1,-1)\"");
            
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, ">");
            
            if (elem->text.text) {
                // TODO: HTML escape
                strbuf_append_str(out, elem->text.text);
            }
            strbuf_append_str(out, "</text>\n");
            break;
        }
        
        case GraphicsType::IMAGE: {
            strbuf_append_format(out, "<image x=\"%.2f\" y=\"%.2f\" "
                               "width=\"%.2f\" height=\"%.2f\" href=\"%s\"",
                          elem->image.pos.x, elem->image.pos.y,
                          elem->image.width, elem->image.height,
                          elem->image.src ? elem->image.src : "");
            emit_svg_transform(elem->transform, out);
            strbuf_append_str(out, "/>\n");
            break;
        }
        
        case GraphicsType::CANVAS:
            // Should not happen - CANVAS is only at root
            break;
    }
}

} // namespace tex
