#include "graph_to_svg.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/strbuf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

using namespace lambda;

// Create default SVG generator options
SvgGeneratorOptions* create_default_svg_options() {
    SvgGeneratorOptions* opts = (SvgGeneratorOptions*)calloc(1, sizeof(SvgGeneratorOptions));
    opts->canvas_padding = 20.0f;
    opts->default_fill = "lightblue";
    opts->default_stroke = "black";
    opts->default_stroke_width = 2.0f;
    opts->font_family = "Arial, sans-serif";
    opts->font_size = 14.0f;
    opts->include_grid = false;
    return opts;
}

// Helper: get attribute from graph element or use default
static const char* get_node_attribute(Element* graph, const char* node_id, 
                                     const char* attr_name, const char* default_value) {
    if (!graph || !graph->children.list) return default_value;
    
    MarkReader reader(graph);
    Item children = reader.getChildren();
    
    if (!is_list(children)) return default_value;
    
    List* child_list = children.list;
    for (size_t i = 0; i < child_list->length; i++) {
        Item child_item = list_get(child_list, i);
        if (!is_element(child_item)) continue;
        
        Element* child = child_item.element;
        if (strcmp(child->tag->chars, "node") != 0) continue;
        
        Item id_item = map_get(child->attrs.map, "id");
        if (!is_string(id_item)) continue;
        if (strcmp(id_item.string_ptr->chars, node_id) != 0) continue;
        
        // found the node, get attribute
        Item attr_item = map_get(child->attrs.map, attr_name);
        if (is_string(attr_item)) {
            return attr_item.string_ptr->chars;
        }
    }
    
    return default_value;
}

// Helper: render node shape
static Element* render_node_shape(MarkBuilder& builder, NodePosition* pos, 
                                  const char* shape, const char* fill, 
                                  const char* stroke, float stroke_width) {
    float x = pos->x - pos->width / 2.0f;
    float y = pos->y - pos->height / 2.0f;
    float cx = pos->x;
    float cy = pos->y;
    float w = pos->width;
    float h = pos->height;
    
    Element* shape_elem = nullptr;
    
    if (strcmp(shape, "circle") == 0) {
        float r = fminf(w, h) / 2.0f;
        shape_elem = builder.createElement("circle");
        builder.setAttribute(shape_elem, "cx", cx);
        builder.setAttribute(shape_elem, "cy", cy);
        builder.setAttribute(shape_elem, "r", r);
    } else if (strcmp(shape, "ellipse") == 0) {
        shape_elem = builder.createElement("ellipse");
        builder.setAttribute(shape_elem, "cx", cx);
        builder.setAttribute(shape_elem, "cy", cy);
        builder.setAttribute(shape_elem, "rx", w / 2.0f);
        builder.setAttribute(shape_elem, "ry", h / 2.0f);
    } else if (strcmp(shape, "diamond") == 0) {
        // diamond as polygon (4 points)
        StringBuf* sb = stringbuf_create();
        stringbuf_appendf(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         cx, y,           // top
                         x + w, cy,       // right
                         cx, y + h,       // bottom
                         x, cy);          // left
        
        shape_elem = builder.createElement("polygon");
        builder.setAttribute(shape_elem, "points", sb->str->chars);
        stringbuf_destroy(sb);
    } else if (strcmp(shape, "hexagon") == 0) {
        // hexagon as polygon (6 points)
        float r = fminf(w, h) / 2.0f;
        StringBuf* sb = stringbuf_create();
        for (int i = 0; i < 6; i++) {
            float angle = (float)i * 3.14159f / 3.0f;
            float px = cx + r * cosf(angle);
            float py = cy + r * sinf(angle);
            stringbuf_appendf(sb, "%.1f,%.1f ", px, py);
        }
        
        shape_elem = builder.createElement("polygon");
        builder.setAttribute(shape_elem, "points", sb->str->chars);
        stringbuf_destroy(sb);
    } else if (strcmp(shape, "triangle") == 0) {
        // triangle pointing up
        StringBuf* sb = stringbuf_create();
        stringbuf_appendf(sb, "%.1f,%.1f %.1f,%.1f %.1f,%.1f",
                         cx, y,           // top
                         x + w, y + h,    // bottom-right
                         x, y + h);       // bottom-left
        
        shape_elem = builder.createElement("polygon");
        builder.setAttribute(shape_elem, "points", sb->str->chars);
        stringbuf_destroy(sb);
    } else {
        // default: rectangle (box)
        shape_elem = builder.createElement("rect");
        builder.setAttribute(shape_elem, "x", x);
        builder.setAttribute(shape_elem, "y", y);
        builder.setAttribute(shape_elem, "width", w);
        builder.setAttribute(shape_elem, "height", h);
        builder.setAttribute(shape_elem, "rx", 5.0f); // rounded corners
    }
    
    // apply styling
    if (shape_elem) {
        builder.setAttribute(shape_elem, "fill", fill);
        builder.setAttribute(shape_elem, "stroke", stroke);
        builder.setAttribute(shape_elem, "stroke-width", stroke_width);
    }
    
    return shape_elem;
}

// Helper: render edge path
static Element* render_edge_path(MarkBuilder& builder, EdgePath* path, 
                                 const char* stroke, float stroke_width) {
    if (!path->points || path->points->length < 2) {
        return nullptr;
    }
    
    // build path data string
    StringBuf* sb = stringbuf_create();
    
    Point2D* first = (Point2D*)arraylist_at(path->points, 0);
    stringbuf_appendf(sb, "M %.1f,%.1f", first->x, first->y);
    
    if (path->is_bezier && path->points->length >= 4) {
        // use cubic bezier curves
        for (size_t i = 1; i + 2 < path->points->length; i += 3) {
            Point2D* cp1 = (Point2D*)arraylist_at(path->points, i);
            Point2D* cp2 = (Point2D*)arraylist_at(path->points, i + 1);
            Point2D* end = (Point2D*)arraylist_at(path->points, i + 2);
            stringbuf_appendf(sb, " C %.1f,%.1f %.1f,%.1f %.1f,%.1f",
                            cp1->x, cp1->y, cp2->x, cp2->y, end->x, end->y);
        }
    } else {
        // straight lines
        for (size_t i = 1; i < path->points->length; i++) {
            Point2D* pt = (Point2D*)arraylist_at(path->points, i);
            stringbuf_appendf(sb, " L %.1f,%.1f", pt->x, pt->y);
        }
    }
    
    Element* path_elem = builder.createElement("path");
    builder.setAttribute(path_elem, "d", sb->str->chars);
    builder.setAttribute(path_elem, "stroke", stroke);
    builder.setAttribute(path_elem, "stroke-width", stroke_width);
    builder.setAttribute(path_elem, "fill", "none");
    
    // add arrow marker if directed
    if (path->directed) {
        builder.setAttribute(path_elem, "marker-end", "url(#arrowhead)");
    }
    
    stringbuf_destroy(sb);
    return path_elem;
}

// Helper: create arrow marker definition
static Element* create_arrow_marker(MarkBuilder& builder, const char* stroke) {
    Element* marker = builder.createElement("marker");
    builder.setAttribute(marker, "id", "arrowhead");
    builder.setAttribute(marker, "markerWidth", 10.0f);
    builder.setAttribute(marker, "markerHeight", 10.0f);
    builder.setAttribute(marker, "refX", 9.0f);
    builder.setAttribute(marker, "refY", 3.0f);
    builder.setAttribute(marker, "orient", "auto");
    
    // arrow shape as polygon
    Element* polygon = builder.createElement("polygon");
    builder.setAttribute(polygon, "points", "0,0 10,3 0,6");
    builder.setAttribute(polygon, "fill", stroke);
    
    builder.appendChild(marker, polygon);
    
    return marker;
}

// Main SVG generation
Element* graph_to_svg_with_options(Element* graph, GraphLayout* layout, 
                                   SvgGeneratorOptions* opts, Input* input) {
    if (!graph || !layout) {
        log_error("graph_to_svg: null graph or layout");
        return nullptr;
    }
    
    log_info("generating SVG from graph layout");
    
    MarkBuilder builder(input);
    
    // create root SVG element
    float svg_width = layout->graph_width + 2 * opts->canvas_padding;
    float svg_height = layout->graph_height + 2 * opts->canvas_padding;
    
    Element* svg = builder.createElement("svg");
    builder.setAttribute(svg, "width", svg_width);
    builder.setAttribute(svg, "height", svg_height);
    builder.setAttribute(svg, "xmlns", "http://www.w3.org/2000/svg");
    
    // create defs section for markers
    Element* defs = builder.createElement("defs");
    Element* arrow_marker = create_arrow_marker(builder, opts->default_stroke);
    builder.appendChild(defs, arrow_marker);
    builder.appendChild(svg, defs);
    
    // create main group with padding offset
    Element* main_group = builder.createElement("g");
    StringBuf* transform_sb = stringbuf_create();
    stringbuf_appendf(transform_sb, "translate(%.1f, %.1f)", 
                     opts->canvas_padding, opts->canvas_padding);
    builder.setAttribute(main_group, "transform", transform_sb->str->chars);
    stringbuf_destroy(transform_sb);
    
    // render edges first (so they appear behind nodes)
    Element* edges_group = builder.createElement("g");
    builder.setAttribute(edges_group, "class", "edges");
    
    for (size_t i = 0; i < layout->edge_paths->length; i++) {
        EdgePath* edge_path = (EdgePath*)arraylist_at(layout->edge_paths, i);
        Element* path_elem = render_edge_path(builder, edge_path, 
                                              opts->default_stroke, 
                                              opts->default_stroke_width);
        if (path_elem) {
            builder.appendChild(edges_group, path_elem);
        }
    }
    builder.appendChild(main_group, edges_group);
    
    // render nodes
    Element* nodes_group = builder.createElement("g");
    builder.setAttribute(nodes_group, "class", "nodes");
    
    for (size_t i = 0; i < layout->node_positions->length; i++) {
        NodePosition* pos = (NodePosition*)arraylist_at(layout->node_positions, i);
        const char* node_id = pos->node_id;
        
        // get node attributes from original graph
        const char* shape = get_node_attribute(graph, node_id, "shape", "box");
        const char* label = get_node_attribute(graph, node_id, "label", node_id);
        const char* fill = get_node_attribute(graph, node_id, "fill", opts->default_fill);
        
        // create node group
        Element* node_group = builder.createElement("g");
        builder.setAttribute(node_group, "class", "node");
        builder.setAttribute(node_group, "id", node_id);
        
        // render shape
        Element* shape_elem = render_node_shape(builder, pos, shape, fill, 
                                               opts->default_stroke, 
                                               opts->default_stroke_width);
        if (shape_elem) {
            builder.appendChild(node_group, shape_elem);
        }
        
        // render label
        Element* text_elem = builder.createElement("text");
        builder.setAttribute(text_elem, "x", pos->x);
        builder.setAttribute(text_elem, "y", pos->y);
        builder.setAttribute(text_elem, "text-anchor", "middle");
        builder.setAttribute(text_elem, "dominant-baseline", "middle");
        builder.setAttribute(text_elem, "font-family", opts->font_family);
        builder.setAttribute(text_elem, "font-size", opts->font_size);
        builder.setAttribute(text_elem, "fill", "black");
        
        // set text content
        Element* text_content = builder.createText(label);
        builder.appendChild(text_elem, text_content);
        builder.appendChild(node_group, text_elem);
        
        builder.appendChild(nodes_group, node_group);
    }
    
    builder.appendChild(main_group, nodes_group);
    builder.appendChild(svg, main_group);
    
    log_info("SVG generation complete: %.1f x %.1f", svg_width, svg_height);
    
    return svg;
}

Element* graph_to_svg(Element* graph, GraphLayout* layout, Input* input) {
    SvgGeneratorOptions* opts = create_default_svg_options();
    Element* result = graph_to_svg_with_options(graph, layout, opts, input);
    free(opts);
    return result;
}
