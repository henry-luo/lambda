#include "graph_to_svg.hpp"
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
    return opts;
}

// Helper: get attribute from graph element or use default
static const char* get_node_attribute(Element* graph, const char* node_id, 
                                     const char* attr_name, const char* default_value) {
    if (!graph) return default_value;
    
    ElementReader graph_reader(graph);
    ElementReader::ChildIterator children = graph_reader.children();
    ItemReader child_item;
    
    while (children.next(&child_item)) {
        if (!child_item.isElement()) continue;
        
        ElementReader child_reader = child_item.asElement();
        const char* tag = child_reader.tagName();
        if (!tag || strcmp(tag, "node") != 0) continue;
        
        ItemReader id_reader = child_reader.get_attr("id");
        if (!id_reader.isString()) continue;
        
        const char* this_id = id_reader.cstring();
        if (strcmp(this_id, node_id) != 0) continue;
        
        // found the node, get attribute
        ItemReader attr_reader = child_reader.get_attr(attr_name);
        if (attr_reader.isString()) {
            return attr_reader.cstring();
        }
        break;
    }
    
    return default_value;
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
    } else {
        // default: rectangle (box)
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
        .final();
}

// Helper: render edge path
static Item render_edge_path(MarkBuilder& builder, Pool* pool, EdgePath* path, 
                             const char* stroke, float stroke_width) {
    if (!path->points || path->points->length < 2) {
        return ItemNull;
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
    path_builder.attr("stroke-width", (double)stroke_width);
    path_builder.attr("fill", "none");
    
    Item path_item = path_builder.final();
    stringbuf_free(sb);
    
    // If directed, add arrowhead as separate polygon (ThorVG doesn't support markers)
    if (path->directed && path->points->length >= 2) {
        // Get last two points to calculate arrow direction
        Point2D* prev = (Point2D*)path->points->data[path->points->length - 2];
        Point2D* last = (Point2D*)path->points->data[path->points->length - 1];
        
        float dx = last->x - prev->x;
        float dy = last->y - prev->y;
        float angle = atan2f(dy, dx);
        
        Item arrow_item = render_arrowhead(builder, last->x, last->y, angle, stroke, 10.0f);
        
        // Return group containing path and arrowhead
        return builder.element("g")
            .child(path_item)
            .child(arrow_item)
            .final();
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
    
    // create root SVG element
    float svg_width = layout->graph_width + 2 * opts->canvas_padding;
    float svg_height = layout->graph_height + 2 * opts->canvas_padding;
    
    // create defs section for markers
    Item arrow_marker = create_arrow_marker(builder, opts->default_stroke);
    Item defs = builder.element("defs")
        .child(arrow_marker)
        .final();
    
    // render edges first (so they appear behind nodes)
    ElementBuilder edges_group_builder = builder.element("g");
    edges_group_builder.attr("class", "edges");
    
    for (int i = 0; i < layout->edge_paths->length; i++) {
        EdgePath* edge_path = (EdgePath*)layout->edge_paths->data[i];
        Item path_item = render_edge_path(builder, pool, edge_path, 
                                          opts->default_stroke, 
                                          opts->default_stroke_width);
        if (path_item.item != 0) {
            edges_group_builder.child(path_item);
        }
    }
    Item edges_group = edges_group_builder.final();
    
    // render nodes
    ElementBuilder nodes_group_builder = builder.element("g");
    nodes_group_builder.attr("class", "nodes");
    
    for (int i = 0; i < layout->node_positions->length; i++) {
        NodePosition* pos = (NodePosition*)layout->node_positions->data[i];
        const char* node_id = pos->node_id;
        
        // get node attributes from original graph
        const char* shape = get_node_attribute(graph, node_id, "shape", "box");
        const char* label = get_node_attribute(graph, node_id, "label", node_id);
        const char* fill = get_node_attribute(graph, node_id, "fill", opts->default_fill);
        
        // render shape
        Item shape_item = render_node_shape(builder, pool, pos, shape, fill, 
                                           opts->default_stroke, 
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
            .attr("fill", "black")
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
        .child(edges_group)
        .child(nodes_group)
        .final();
    stringbuf_free(transform_sb);
    
    // create root SVG element with all children
    Item svg = builder.element("svg")
        .attr("width", (double)svg_width)
        .attr("height", (double)svg_height)
        .attr("xmlns", "http://www.w3.org/2000/svg")
        .child(defs)
        .child(main_group)
        .final();
    
    log_info("SVG generation complete: %.1f x %.1f", svg_width, svg_height);
    
    return svg;
}

Item graph_to_svg(Element* graph, GraphLayout* layout, Input* input) {
    SvgGeneratorOptions* opts = create_default_svg_options();
    Item result = graph_to_svg_with_options(graph, layout, opts, input);
    free(opts);
    return result;
}
