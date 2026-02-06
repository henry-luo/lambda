#include "graph_to_svg.hpp"
#include "graph_theme.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/stringbuf.h"
#include "../lib/mempool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Create default SVG generator options
SvgGeneratorOptions* create_default_svg_options() {
    SvgGeneratorOptions* opts = (SvgGeneratorOptions*)calloc(1, sizeof(SvgGeneratorOptions));
    opts->canvas_padding = 20.0f;
    opts->default_fill = "lightblue";
    opts->default_stroke = "black";
    opts->default_stroke_width = 2.0f;
    opts->font_family = "Arial";  // simple name without fallback - ThorVG doesn't handle CSS font lists
    opts->font_size = 14.0f;
    opts->include_grid = false;
    opts->theme = NULL;  // Use default colors if no theme
    return opts;
}

// Create SVG options with a theme
SvgGeneratorOptions* create_themed_svg_options(const char* theme_name) {
    SvgGeneratorOptions* opts = (SvgGeneratorOptions*)calloc(1, sizeof(SvgGeneratorOptions));
    opts->canvas_padding = 20.0f;
    opts->font_family = "Arial";
    opts->font_size = 14.0f;
    opts->default_stroke_width = 2.0f;
    opts->include_grid = false;

    // Apply theme
    const DiagramTheme* theme = get_theme_by_name(theme_name);
    opts->theme = theme;
    opts->default_fill = theme->node_fill;
    opts->default_stroke = theme->node_stroke;

    return opts;
}

// Helper functions to get themed colors
static const char* get_themed_node_fill(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->node_fill;
    return opts->default_fill;
}

static const char* get_themed_node_stroke(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->node_stroke;
    return opts->default_stroke;
}

static const char* get_themed_text_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->text;
    return "black";
}

static const char* get_themed_line_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->line;
    return opts->default_stroke;
}

static const char* get_themed_arrow_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->arrow;
    return opts->default_stroke;
}

static const char* get_themed_bg_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->bg;
    return "#ffffff";
}

static const char* get_themed_group_header_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->group_header;
    return "#e8e8e8";
}

static const char* get_themed_surface_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->surface;
    return "#f5f5f5";
}

static const char* get_themed_text_secondary_color(SvgGeneratorOptions* opts) {
    if (opts->theme) return opts->theme->text_secondary;
    return "#666666";
}

// Forward declaration
static const char* get_node_attribute_recursive(ElementReader& reader, const char* node_id,
                                                const char* attr_name, const char* default_value);

// Helper: recursively search for node attribute in element and its subgraphs
static const char* get_node_attribute_recursive(ElementReader& reader, const char* node_id,
                                                const char* attr_name, const char* default_value) {
    ElementReader::ChildIterator children = reader.children();
    ItemReader child_item;

    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;

        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag) continue;

        if (strcmp(tag, "node") == 0) {
            ItemReader id_reader = child_reader.get_attr("id");
            if (!id_reader.isString()) continue;

            const char* this_id = id_reader.cstring();
            if (strcmp(this_id, node_id) != 0) continue;

            // found the node, get attribute
            ItemReader attr_reader = child_reader.get_attr(attr_name);
            if (attr_reader.isString()) {
                return attr_reader.cstring();
            }
            return default_value;
        } else if (strcmp(tag, "subgraph") == 0) {
            // recurse into subgraph
            const char* result = get_node_attribute_recursive(child_reader, node_id, attr_name, nullptr);
            if (result) return result;
        }
    }

    return default_value;
}

// Helper: get attribute from graph element or use default (searches recursively into subgraphs)
static const char* get_node_attribute(Element* graph, const char* node_id,
                                     const char* attr_name, const char* default_value) {
    if (!graph) return default_value;

    ElementReader graph_reader(graph);
    return get_node_attribute_recursive(graph_reader, node_id, attr_name, default_value);
}

// Helper: render node shape - returns Item (Element)
static Item render_node_shape(MarkBuilder& builder, Pool* pool, NodePosition* pos,
                              const char* shape, const char* fill,
                              const char* stroke, float stroke_width) {
    float x = pos->x - pos->width / 2.0f;
    float y = pos->y - pos->height / 2.0f;
    float cx = pos->x;
    float cy = pos->y;
    float w = pos->width;
    float h = pos->height;

    if (strcmp(shape, "circle") == 0) {
        float r = fminf(w, h) / 2.0f;
        return builder.element("circle")
            .attr("cx", (double)cx)
            .attr("cy", (double)cy)
            .attr("r", (double)r)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
    } else if (strcmp(shape, "ellipse") == 0) {
        return builder.element("ellipse")
            .attr("cx", (double)cx)
            .attr("cy", (double)cy)
            .attr("rx", (double)(w / 2.0f))
            .attr("ry", (double)(h / 2.0f))
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
    } else if (strcmp(shape, "diamond") == 0) {
        // diamond as polygon (4 points)
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         cx, y,           // top
                         x + w, cy,       // right
                         cx, y + h,       // bottom
                         x, cy);          // left

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "hexagon") == 0) {
        // hexagon as polygon (6 points)
        float r = fminf(w, h) / 2.0f;
        StringBuf* sb = stringbuf_new(pool);
        for (int i = 0; i < 6; i++) {
            float angle = (float)i * 3.14159f / 3.0f - 3.14159f / 6.0f;
            float px = cx + r * cosf(angle);
            float py = cy + r * sinf(angle);
            if (i > 0) stringbuf_append_str(sb, " ");
            stringbuf_append_format(sb, "%.1f,%.1f", px, py);
        }

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "triangle") == 0) {
        // triangle pointing up
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         cx, y,           // top
                         x + w, y + h,    // bottom-right
                         x, y + h);       // bottom-left

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "stadium") == 0 || strcmp(shape, "rounded") == 0) {
        // stadium/pill shape: rectangle with fully rounded ends (half-circles)
        float r = h / 2.0f;  // radius is half the height
        return builder.element("rect")
            .attr("x", (double)x)
            .attr("y", (double)y)
            .attr("width", (double)w)
            .attr("height", (double)h)
            .attr("rx", (double)r)  // fully rounded ends
            .attr("ry", (double)r)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
    } else if (strcmp(shape, "cylinder") == 0) {
        // cylinder: 3D-ish database shape with ellipse on top/bottom
        // Draw as a group with rect body and two ellipses
        float ellipse_ry = h * 0.15f;  // ellipse height is 15% of total height
        float body_y = y + ellipse_ry;
        float body_h = h - 2 * ellipse_ry;

        // Build SVG path for cylinder (body + top ellipse)
        // Using path for the body (rounded rectangle with ellipse caps)
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb,
            "M %.1f,%.1f "
            "L %.1f,%.1f "
            "A %.1f,%.1f 0 0,0 %.1f,%.1f "
            "L %.1f,%.1f "
            "A %.1f,%.1f 0 0,0 %.1f,%.1f "
            "Z",
            x, body_y,                              // start left side
            x, body_y + body_h,                     // down to bottom
            w / 2.0f, ellipse_ry, x + w, body_y + body_h,  // bottom ellipse arc
            x + w, body_y,                          // up right side
            w / 2.0f, ellipse_ry, x, body_y);       // top ellipse arc (back)

        Item body = builder.element("path")
            .attr("d", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);

        // Top ellipse (visible lid)
        Item top_ellipse = builder.element("ellipse")
            .attr("cx", (double)cx)
            .attr("cy", (double)body_y)
            .attr("rx", (double)(w / 2.0f))
            .attr("ry", (double)ellipse_ry)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        return builder.element("g")
            .child(body)
            .child(top_ellipse)
            .final();
    } else if (strcmp(shape, "subroutine") == 0) {
        // subroutine: rectangle with double vertical lines on sides
        float inset = w * 0.1f;  // 10% inset for double lines

        // Main rectangle
        Item rect_main = builder.element("rect")
            .attr("x", (double)x)
            .attr("y", (double)y)
            .attr("width", (double)w)
            .attr("height", (double)h)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        // Left vertical line
        Item line_left = builder.element("line")
            .attr("x1", (double)(x + inset))
            .attr("y1", (double)y)
            .attr("x2", (double)(x + inset))
            .attr("y2", (double)(y + h))
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        // Right vertical line
        Item line_right = builder.element("line")
            .attr("x1", (double)(x + w - inset))
            .attr("y1", (double)y)
            .attr("x2", (double)(x + w - inset))
            .attr("y2", (double)(y + h))
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        return builder.element("g")
            .child(rect_main)
            .child(line_left)
            .child(line_right)
            .final();
    } else if (strcmp(shape, "doublecircle") == 0) {
        // double circle: two concentric circles
        float r = fminf(w, h) / 2.0f;
        float inner_r = r * 0.8f;  // inner circle is 80% of outer

        Item outer = builder.element("circle")
            .attr("cx", (double)cx)
            .attr("cy", (double)cy)
            .attr("r", (double)r)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        Item inner = builder.element("circle")
            .attr("cx", (double)cx)
            .attr("cy", (double)cy)
            .attr("r", (double)inner_r)
            .attr("fill", "none")
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();

        return builder.element("g")
            .child(outer)
            .child(inner)
            .final();
    } else if (strcmp(shape, "trapezoid") == 0) {
        // trapezoid: wider at bottom, narrower at top  [/text\]
        float inset = w * 0.15f;  // 15% inset on each side at top
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         x + inset, y,         // top-left (inset)
                         x + w - inset, y,     // top-right (inset)
                         x + w, y + h,         // bottom-right
                         x, y + h);            // bottom-left

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "trapezoid-alt") == 0 || strcmp(shape, "inv_trapezoid") == 0) {
        // inverted trapezoid: wider at top, narrower at bottom  [\text/]
        float inset = w * 0.15f;  // 15% inset on each side at bottom
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         x, y,                     // top-left
                         x + w, y,                 // top-right
                         x + w - inset, y + h,     // bottom-right (inset)
                         x + inset, y + h);        // bottom-left (inset)

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "asymmetric") == 0) {
        // asymmetric: flag-like shape with pointed right side  >text]
        float point_offset = w * 0.15f;
        StringBuf* sb = stringbuf_new(pool);
        stringbuf_append_format(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         x, y,                         // top-left
                         x + w, y,                     // top-right
                         x + w, y + h,                 // bottom-right
                         x, y + h,                     // bottom-left
                         x + point_offset, cy);        // left point

        Item result = builder.element("polygon")
            .attr("points", sb->str->chars)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
        stringbuf_free(sb);
        return result;
    } else if (strcmp(shape, "box") == 0) {
        // box: simple rectangle with no rounded corners
        return builder.element("rect")
            .attr("x", (double)x)
            .attr("y", (double)y)
            .attr("width", (double)w)
            .attr("height", (double)h)
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
    } else {
        // default: rectangle with rounded corners
        return builder.element("rect")
            .attr("x", (double)x)
            .attr("y", (double)y)
            .attr("width", (double)w)
            .attr("height", (double)h)
            .attr("rx", 5.0)  // rounded corners
            .attr("fill", fill)
            .attr("stroke", stroke)
            .attr("stroke-width", (double)stroke_width)
            .final();
    }
}

// Helper: render arrowhead as a polygon at the end of an edge
// ThorVG doesn't support SVG markers, so we draw arrows manually
static Item render_arrowhead(MarkBuilder& builder, float x, float y, float angle,
                              const char* fill, float size) {
    // Arrow points: tip at (x,y), pointing in direction of angle
    // We create a triangle pointing in the direction of the edge
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    // Arrow base is behind the tip
    float base_x = x - size * cos_a;
    float base_y = y - size * sin_a;

    // Arrow wings perpendicular to direction
    float wing_size = size * 0.5f;
    float wing1_x = base_x - wing_size * sin_a;
    float wing1_y = base_y + wing_size * cos_a;
    float wing2_x = base_x + wing_size * sin_a;
    float wing2_y = base_y - wing_size * cos_a;

    // Format points string using stack buffer
    char points_buf[128];
    snprintf(points_buf, sizeof(points_buf), "%.1f,%.1f %.1f,%.1f %.1f,%.1f",
             x, y, wing1_x, wing1_y, wing2_x, wing2_y);

    return builder.element("polygon")
        .attr("points", points_buf)
        .attr("fill", fill)
        .attr("stroke", fill)
        .attr("stroke-width", 1.0)
        .final();
}

// Helper: render edge path with style support
static Item render_edge_path(MarkBuilder& builder, Pool* pool, EdgePath* path,
                             const char* stroke, float stroke_width) {
    if (!path->points || path->points->length < 2) {
        return ItemNull;
    }

    // determine stroke width based on edge style
    float actual_stroke_width = stroke_width;
    const char* edge_style = path->edge_style ? path->edge_style : "solid";
    if (strcmp(edge_style, "thick") == 0) {
        actual_stroke_width = stroke_width * 2.0f;  // thick edges are 2x width
    }

    // build path data string
    StringBuf* sb = stringbuf_new(pool);

    Point2D* first = (Point2D*)path->points->data[0];
    stringbuf_append_format(sb, "M %.1f,%.1f", first->x, first->y);

    if (path->is_bezier && path->points->length >= 4) {
        // use cubic bezier curves
        for (int i = 1; i + 2 < path->points->length; i += 3) {
            Point2D* cp1 = (Point2D*)path->points->data[i];
            Point2D* cp2 = (Point2D*)path->points->data[i + 1];
            Point2D* end = (Point2D*)path->points->data[i + 2];
            stringbuf_append_format(sb, " C %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                            cp1->x, cp1->y, cp2->x, cp2->y, end->x, end->y);
        }
    } else {
        // straight lines
        for (int i = 1; i < path->points->length; i++) {
            Point2D* pt = (Point2D*)path->points->data[i];
            stringbuf_append_format(sb, " L %.1f,%.1f", pt->x, pt->y);
        }
    }

    ElementBuilder path_builder = builder.element("path");
    path_builder.attr("d", sb->str->chars);
    path_builder.attr("stroke", stroke);
    path_builder.attr("stroke-width", (double)actual_stroke_width);
    path_builder.attr("fill", "none");

    // apply dash pattern for dotted edges
    if (strcmp(edge_style, "dotted") == 0) {
        path_builder.attr("stroke-dasharray", "5,5");
    }

    Item path_item = path_builder.final();
    stringbuf_free(sb);

    // check if we need arrows at either end
    bool need_arrow_end = path->directed || path->arrow_end;
    bool need_arrow_start = path->arrow_start;

    if ((need_arrow_end || need_arrow_start) && path->points->length >= 2) {
        ElementBuilder group_builder = builder.element("g");
        group_builder.child(path_item);

        // arrow at end
        if (need_arrow_end) {
            Point2D* prev = (Point2D*)path->points->data[path->points->length - 2];
            Point2D* last = (Point2D*)path->points->data[path->points->length - 1];

            float dx = last->x - prev->x;
            float dy = last->y - prev->y;
            float angle = atan2f(dy, dx);

            Item arrow_end = render_arrowhead(builder, last->x, last->y, angle, stroke, 10.0f);
            group_builder.child(arrow_end);
        }

        // arrow at start (for bidirectional edges)
        if (need_arrow_start) {
            Point2D* first_pt = (Point2D*)path->points->data[0];
            Point2D* second = (Point2D*)path->points->data[1];

            float dx = first_pt->x - second->x;
            float dy = first_pt->y - second->y;
            float angle = atan2f(dy, dx);

            Item arrow_start = render_arrowhead(builder, first_pt->x, first_pt->y, angle, stroke, 10.0f);
            group_builder.child(arrow_start);
        }

        return group_builder.final();
    }

    return path_item;
}

// Helper: create arrow marker definition
static Item create_arrow_marker(MarkBuilder& builder, const char* stroke) {
    // arrow shape as polygon
    Item polygon = builder.element("polygon")
        .attr("points", "0,0 10,3 0,6")
        .attr("fill", stroke)
        .final();

    return builder.element("marker")
        .attr("id", "arrowhead")
        .attr("markerWidth", 10.0)
        .attr("markerHeight", 10.0)
        .attr("refX", 9.0)
        .attr("refY", 3.0)
        .attr("orient", "auto")
        .child(polygon)
        .final();
}

// Main SVG generation
Item graph_to_svg_with_options(Element* graph, GraphLayout* layout,
                               SvgGeneratorOptions* opts, Input* input) {
    if (!graph || !layout) {
        log_error("graph_to_svg: null graph or layout");
        return ItemNull;
    }

    log_info("generating SVG from graph layout");

    MarkBuilder builder(input);
    Pool* pool = builder.pool();

    // Get themed colors
    const char* node_fill = get_themed_node_fill(opts);
    const char* node_stroke = get_themed_node_stroke(opts);
    const char* text_color = get_themed_text_color(opts);
    const char* line_color = get_themed_line_color(opts);
    const char* arrow_color = get_themed_arrow_color(opts);
    const char* bg_color = get_themed_bg_color(opts);

    // create root SVG element
    float svg_width = layout->graph_width + 2 * opts->canvas_padding;
    float svg_height = layout->graph_height + 2 * opts->canvas_padding;

    // create defs section for markers
    Item arrow_marker = create_arrow_marker(builder, arrow_color);
    Item defs = builder.element("defs")
        .child(arrow_marker)
        .final();

    // create background rect if using a theme with non-white background
    Item bg_rect = ItemNull;
    if (opts->theme) {
        bg_rect = builder.element("rect")
            .attr("width", (double)svg_width)
            .attr("height", (double)svg_height)
            .attr("fill", bg_color)
            .final();
    }

    // render edges first (so they appear behind nodes)
    ElementBuilder edges_group_builder = builder.element("g");
    edges_group_builder.attr("class", "edges");

    for (int i = 0; i < layout->edge_paths->length; i++) {
        EdgePath* edge_path = (EdgePath*)layout->edge_paths->data[i];
        Item path_item = render_edge_path(builder, pool, edge_path,
                                          line_color,
                                          opts->default_stroke_width);
        if (path_item.item != 0) {
            edges_group_builder.child(path_item);
        }
    }
    Item edges_group = edges_group_builder.final();

    // render subgraphs (boxes with labels, behind nodes but in front of edges)
    ElementBuilder subgraphs_group_builder = builder.element("g");
    subgraphs_group_builder.attr("class", "subgraphs");

    const char* subgraph_fill = get_themed_surface_color(opts);
    const char* subgraph_stroke = get_themed_node_stroke(opts);
    const char* subgraph_header_fill = get_themed_group_header_color(opts);
    const char* subgraph_text_color = get_themed_text_secondary_color(opts);

    if (layout->subgraph_positions) {
        for (int i = 0; i < layout->subgraph_positions->length; i++) {
            SubgraphPosition* sgpos = (SubgraphPosition*)layout->subgraph_positions->data[i];

            // create subgraph group
            ElementBuilder sg_builder = builder.element("g");
            sg_builder.attr("class", "subgraph");
            sg_builder.attr("id", sgpos->subgraph_id);

            // main box with rounded corners
            Item sg_rect = builder.element("rect")
                .attr("x", (double)sgpos->x)
                .attr("y", (double)sgpos->y)
                .attr("width", (double)sgpos->width)
                .attr("height", (double)sgpos->height)
                .attr("rx", 5.0)
                .attr("ry", 5.0)
                .attr("fill", subgraph_fill)
                .attr("stroke", subgraph_stroke)
                .attr("stroke-width", 1.0)
                .final();
            sg_builder.child(sg_rect);

            // header bar with label
            if (sgpos->label && sgpos->label_height > 0) {
                Item header_rect = builder.element("rect")
                    .attr("x", (double)sgpos->x)
                    .attr("y", (double)sgpos->y)
                    .attr("width", (double)sgpos->width)
                    .attr("height", (double)sgpos->label_height)
                    .attr("rx", 5.0)
                    .attr("ry", 5.0)
                    .attr("fill", subgraph_header_fill)
                    .final();
                sg_builder.child(header_rect);

                // label text
                float label_x = sgpos->x + 8.0f;  // padding from left
                float label_y = sgpos->y + sgpos->label_height * 0.7f;  // baseline adjust

                Item sg_label = builder.element("text")
                    .attr("x", (double)label_x)
                    .attr("y", (double)label_y)
                    .attr("font-family", opts->font_family)
                    .attr("font-size", (double)(opts->font_size - 1))
                    .attr("font-weight", "600")
                    .attr("fill", subgraph_text_color)
                    .text(sgpos->label)
                    .final();
                sg_builder.child(sg_label);
            }

            Item sg_group = sg_builder.final();
            subgraphs_group_builder.child(sg_group);
        }
    }
    Item subgraphs_group = subgraphs_group_builder.final();

    // render nodes
    ElementBuilder nodes_group_builder = builder.element("g");
    nodes_group_builder.attr("class", "nodes");

    for (int i = 0; i < layout->node_positions->length; i++) {
        NodePosition* pos = (NodePosition*)layout->node_positions->data[i];
        const char* node_id = pos->node_id;

        // get node attributes from original graph
        const char* shape = get_node_attribute(graph, node_id, "shape", "box");
        const char* label = get_node_attribute(graph, node_id, "label", node_id);
        const char* fill = get_node_attribute(graph, node_id, "fill", node_fill);

        // render shape with themed colors
        Item shape_item = render_node_shape(builder, pool, pos, shape, fill,
                                           node_stroke,
                                           opts->default_stroke_width);

        // render label with manual centering
        // ThorVG doesn't support text-anchor/dominant-baseline, so we calculate offsets manually
        // Estimate text width: average character width is approximately 0.5-0.6 of font size
        size_t label_len = strlen(label);
        float text_width = label_len * opts->font_size * 0.55f;
        // Offset x by half the text width to center horizontally
        float text_x = pos->x - text_width / 2.0f;
        // Offset y by approximately 0.35 * font_size to center vertically (baseline adjustment)
        float text_y = pos->y + opts->font_size * 0.35f;

        Item text_item = builder.element("text")
            .attr("x", (double)text_x)
            .attr("y", (double)text_y)
            .attr("font-family", opts->font_family)
            .attr("font-size", (double)opts->font_size)
            .attr("fill", text_color)
            .text(label)
            .final();

        // create node group
        Item node_group = builder.element("g")
            .attr("class", "node")
            .attr("id", node_id)
            .child(shape_item)
            .child(text_item)
            .final();

        nodes_group_builder.child(node_group);
    }
    Item nodes_group = nodes_group_builder.final();

    // create main group with padding offset
    StringBuf* transform_sb = stringbuf_new(pool);
    stringbuf_append_format(transform_sb, "translate(%.1f, %.1f)",
                     opts->canvas_padding, opts->canvas_padding);

    Item main_group = builder.element("g")
        .attr("transform", transform_sb->str->chars)
        .child(subgraphs_group)
        .child(edges_group)
        .child(nodes_group)
        .final();
    stringbuf_free(transform_sb);

    // create root SVG element with all children
    ElementBuilder svg_builder = builder.element("svg");
    svg_builder.attr("width", (double)svg_width);
    svg_builder.attr("height", (double)svg_height);
    svg_builder.attr("xmlns", "http://www.w3.org/2000/svg");
    svg_builder.child(defs);

    // Add background rect if using theme
    if (bg_rect.item != 0) {
        svg_builder.child(bg_rect);
    }

    svg_builder.child(main_group);
    Item svg = svg_builder.final();

    log_info("SVG generation complete: %.1f x %.1f", svg_width, svg_height);

    return svg;
}

Item graph_to_svg(Element* graph, GraphLayout* layout, Input* input) {
    SvgGeneratorOptions* opts = create_default_svg_options();
    Item result = graph_to_svg_with_options(graph, layout, opts, input);
    free(opts);
    return result;
}
