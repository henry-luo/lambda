// tex_graphics_picture.cpp - Picture environment builder implementation
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md
//
// NOTE: This is a stub implementation. Full implementation requires
// integration with the document model parsing infrastructure.

#include "tex_graphics_picture.hpp"
#include "tex_document_model.hpp"
#include "lib/log.h"
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace tex {

// Forward declarations
static void process_picture_children(PictureState* state, const ElementReader& elem);
static const char* extract_first_text(const ElementReader& elem);
static GraphicsElement* create_line_from_slope(PictureState* state, int dx, int dy, float length);
static GraphicsElement* create_circle(PictureState* state, float diameter, bool filled);
static GraphicsElement* process_put_content(PictureState* state, const ElementReader& elem);

// ============================================================================
// State Initialization
// ============================================================================

void picture_state_init(PictureState* state, Arena* arena, TexDocumentModel* doc) {
    state->arena = arena;
    state->doc = doc;
    state->canvas = nullptr;
    state->current_group = nullptr;
    state->unitlength = 1.0f;      // Default 1pt
    state->line_thickness = 0.4f;  // Default thin line
    state->thin_line = 0.4f;
    state->thick_line = 0.8f;
    state->current_x = 0;
    state->current_y = 0;
    state->stroke_color = "#000000";
    state->fill_color = "none";
}

// ============================================================================
// Coordinate Parsing
// ============================================================================

// Skip whitespace
static const char* skip_ws(const char* s) {
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    return s;
}

// Parse a floating point number
static const char* parse_float(const char* s, float* out) {
    s = skip_ws(s);
    char* end;
    *out = strtof(s, &end);
    return end > s ? end : nullptr;
}

// Parse integer
static const char* parse_int(const char* s, int* out) {
    s = skip_ws(s);
    char* end;
    *out = (int)strtol(s, &end, 10);
    return end > s ? end : nullptr;
}

bool parse_coord_pair(const char* str, float* x, float* y) {
    if (!str) return false;
    
    str = skip_ws(str);
    
    // Skip optional opening paren
    if (*str == '(') str++;
    
    str = parse_float(str, x);
    if (!str) return false;
    
    str = skip_ws(str);
    if (*str == ',') str++;
    
    str = parse_float(str, y);
    if (!str) return false;
    
    return true;
}

bool parse_slope_pair(const char* str, int* dx, int* dy) {
    if (!str) return false;
    
    str = skip_ws(str);
    if (*str == '(') str++;
    
    str = parse_int(str, dx);
    if (!str) return false;
    
    str = skip_ws(str);
    if (*str == ',') str++;
    
    str = parse_int(str, dy);
    if (!str) return false;
    
    // LaTeX picture restricts slopes to -6..6 for \line (but \vector is more restricted)
    if (*dx < -6 || *dx > 6 || *dy < -6 || *dy > 6) {
        log_debug("picture: slope out of range: (%d,%d)", *dx, *dy);
    }
    
    return true;
}

float parse_picture_dimension(const char* str, float unitlength) {
    if (!str) return 0;
    
    str = skip_ws(str);
    
    char* end;
    float val = strtof(str, &end);
    if (end == str) return 0;
    
    // Check for unit suffix
    end = (char*)skip_ws(end);
    if (strncmp(end, "pt", 2) == 0) {
        return val;  // Already in pt
    } else if (strncmp(end, "mm", 2) == 0) {
        return val * 2.845f;  // mm to pt
    } else if (strncmp(end, "cm", 2) == 0) {
        return val * 28.45f;  // cm to pt
    } else if (strncmp(end, "in", 2) == 0) {
        return val * 72.27f;  // in to pt
    } else if (strncmp(end, "em", 2) == 0) {
        return val * 10.0f;   // Approximate em
    } else if (strncmp(end, "ex", 2) == 0) {
        return val * 4.5f;    // Approximate ex
    }
    
    // No unit - use unitlength
    return val * unitlength;
}

// ============================================================================
// Picture Builder
// ============================================================================

GraphicsElement* graphics_build_picture(const ElementReader& elem,
                                         Arena* arena,
                                         TexDocumentModel* doc) {
    PictureState state;
    picture_state_init(&state, arena, doc);
    
    // Get picture size: default values
    float width = 100.0f;
    float height = 100.0f;
    
    // Try to get size from element attributes
    if (elem.has_attr("width")) {
        width = (float)elem.get_int_attr("width", 100);
    }
    if (elem.has_attr("height")) {
        height = (float)elem.get_int_attr("height", 100);
    }
    if (elem.has_attr("size")) {
        const char* size_str = elem.get_attr_string("size");
        if (size_str) {
            parse_coord_pair(size_str, &width, &height);
        }
    }
    
    // Convert to pt using unitlength
    width *= state.unitlength;
    height *= state.unitlength;
    
    // Get optional offset
    float origin_x = 0;
    float origin_y = 0;
    if (elem.has_attr("offset")) {
        const char* offset_str = elem.get_attr_string("offset");
        if (offset_str) {
            parse_coord_pair(offset_str, &origin_x, &origin_y);
            origin_x *= state.unitlength;
            origin_y *= state.unitlength;
        }
    }
    
    // Create canvas
    state.canvas = graphics_canvas(arena, width, height, origin_x, origin_y, state.unitlength);
    state.current_group = state.canvas;
    
    // Process child elements - may be wrapped in paragraph
    process_picture_children(&state, elem);
    
    log_debug("graphics_build_picture: created canvas %.1fx%.1f", width, height);
    
    return state.canvas;
}

// Process children of a picture element (handles paragraph wrapper)
static void process_picture_children(PictureState* state, const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        // Skip pure whitespace text nodes
        if (child.isString()) {
            const char* text = child.cstring();
            if (text) {
                // Skip whitespace-only strings
                bool all_whitespace = true;
                for (const char* p = text; *p; p++) {
                    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                        all_whitespace = false;
                        break;
                    }
                }
                if (all_whitespace) continue;
                
                // Check if this is a coordinate like (100,50)
                // Just log it for now - coordinates are parsed from context
            }
            continue;
        }
        
        if (!child.isElement()) continue;
        
        ElementReader child_elem = child.asElement();
        const char* tag = child_elem.tagName();
        if (!tag) continue;
        
        // Handle paragraph wrapper - recurse into it
        if (strcmp(tag, "paragraph") == 0) {
            process_picture_children(state, child_elem);
            continue;
        }
        
        // Handle picture commands - they need to peek at following siblings
        if (strcmp(tag, "put") == 0) {
            // \put(x,y){content} - next sibling should be coordinate, then content group
            float x = 0, y = 0;
            GraphicsElement* content = nullptr;
            
            // Peek next items for coordinate and content
            ItemReader next_item;
            while (iter.next(&next_item)) {
                if (next_item.isString()) {
                    const char* text = next_item.cstring();
                    if (text && text[0] == '(') {
                        // Parse coordinate
                        parse_coord_pair(text, &x, &y);
                    }
                } else if (next_item.isElement()) {
                    ElementReader content_elem = next_item.asElement();
                    const char* content_tag = content_elem.tagName();
                    if (content_tag && strcmp(content_tag, "curly_group") == 0) {
                        // This is the content group - process it
                        content = process_put_content(state, content_elem);
                        break;
                    }
                }
            }
            
            // Create translated group for the content
            if (content) {
                Transform2D trans = Transform2D::translate(x * state->unitlength, y * state->unitlength);
                GraphicsElement* group = graphics_group(state->arena, &trans);
                graphics_append_child(group, content);
                graphics_append_child(state->current_group, group);
                log_debug("picture_cmd_put: placed at (%.1f, %.1f)", x, y);
            }
        }
        else if (strcmp(tag, "multiput") == 0) {
            picture_cmd_multiput(state, child_elem);
        }
        else if (strcmp(tag, "line") == 0) {
            // \line(dx,dy){length} - next siblings are slope and length
            int dx = 1, dy = 0;
            float length = 10.0f;
            
            ItemReader next_item;
            while (iter.next(&next_item)) {
                if (next_item.isString()) {
                    const char* text = next_item.cstring();
                    if (text && text[0] == '(') {
                        // Parse slope
                        parse_slope_pair(text, &dx, &dy);
                    }
                } else if (next_item.isElement()) {
                    ElementReader len_elem = next_item.asElement();
                    const char* len_tag = len_elem.tagName();
                    if (len_tag && strcmp(len_tag, "curly_group") == 0) {
                        // Extract length from curly group
                        const char* len_text = extract_first_text(len_elem);
                        if (len_text) {
                            length = (float)atof(len_text);
                        }
                        break;
                    }
                }
            }
            
            // Create line element
            GraphicsElement* gfx = create_line_from_slope(state, dx, dy, length);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "circle") == 0 || strcmp(tag, "circle*") == 0) {
            // \circle{diameter} or \circle*{diameter}
            float diameter = 10.0f;
            bool filled = (strcmp(tag, "circle*") == 0);
            
            // Get diameter from children
            auto circle_iter = child_elem.children();
            ItemReader circle_child;
            while (circle_iter.next(&circle_child)) {
                if (circle_child.isString()) {
                    const char* text = circle_child.cstring();
                    if (text && text[0] != '\0') {
                        diameter = (float)atof(text);
                    }
                }
            }
            
            GraphicsElement* gfx = create_circle(state, diameter, filled);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "oval") == 0) {
            GraphicsElement* gfx = picture_cmd_oval(state, child_elem);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "qbezier") == 0) {
            GraphicsElement* gfx = picture_cmd_qbezier(state, child_elem);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "framebox") == 0) {
            GraphicsElement* gfx = picture_cmd_framebox(state, child_elem);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "makebox") == 0) {
            GraphicsElement* gfx = picture_cmd_makebox(state, child_elem);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "dashbox") == 0) {
            GraphicsElement* gfx = picture_cmd_dashbox(state, child_elem);
            if (gfx) graphics_append_child(state->current_group, gfx);
        }
        else if (strcmp(tag, "thinlines") == 0) {
            state->line_thickness = state->thin_line;
        }
        else if (strcmp(tag, "thicklines") == 0) {
            state->line_thickness = state->thick_line;
        }
        else if (strcmp(tag, "linethickness") == 0) {
            // \linethickness{dim}
            const char* dim = child_elem.get_attr_string("dim");
            if (dim) {
                state->line_thickness = parse_picture_dimension(dim, state->unitlength);
            }
        }
        else if (strcmp(tag, "curly_group") == 0) {
            // Content group - may contain nested commands
            process_picture_children(state, child_elem);
        }
        else {
            log_debug("graphics_build_picture: unknown command '%s'", tag);
        }
    }
}

// Helper to extract first text content from an element
static const char* extract_first_text(const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            return child.cstring();
        }
    }
    return nullptr;
}

// Create a line element from slope and length
static GraphicsElement* create_line_from_slope(PictureState* state, int dx, int dy, float length) {
    float x1 = 0, y1 = 0;
    float x2, y2;
    
    if (dx == 0) {
        // Vertical line
        x2 = 0;
        y2 = (dy > 0) ? length : -length;
    } else {
        // Use length as horizontal span
        x2 = (dx > 0) ? length : -length;
        y2 = x2 * ((float)dy / (float)dx);
    }
    
    // Convert to pt using unitlength
    x2 *= state->unitlength;
    y2 *= state->unitlength;
    
    GraphicsElement* line = graphics_line(state->arena, x1, y1, x2, y2);
    line->style.stroke_color = state->stroke_color;
    line->style.stroke_width = state->line_thickness;
    
    log_debug("picture_cmd_line: slope(%d,%d) len=%.1f -> (%.1f,%.1f)-(%.1f,%.1f)",
             dx, dy, length, x1, y1, x2, y2);
    
    return line;
}

// Create a circle element
static GraphicsElement* create_circle(PictureState* state, float diameter, bool filled) {
    float radius = (diameter / 2.0f) * state->unitlength;
    GraphicsElement* circle = graphics_circle(state->arena, 0, 0, radius, filled);
    
    if (filled) {
        circle->style.fill_color = state->stroke_color;
        circle->style.stroke_color = "none";
    } else {
        circle->style.stroke_color = state->stroke_color;
        circle->style.stroke_width = state->line_thickness;
        circle->style.fill_color = "none";
    }
    
    log_debug("picture_cmd_circle: diameter=%.1f filled=%d", diameter, filled);
    
    return circle;
}

// Process content of a \put command (the curly group)
static GraphicsElement* process_put_content(PictureState* state, const ElementReader& elem) {
    // Save current group
    GraphicsElement* saved_group = state->current_group;
    
    // Create a temporary group for the content
    GraphicsElement* content_group = graphics_group(state->arena, nullptr);
    state->current_group = content_group;
    
    // Process children
    process_picture_children(state, elem);
    
    // Restore current group
    state->current_group = saved_group;
    
    return content_group;
}

// ============================================================================
// Command Handlers (Stub implementations)
// ============================================================================

void picture_cmd_put(PictureState* state, const ElementReader& elem) {
    // \put(x,y){content}
    float x = 0, y = 0;
    
    // Get position from attributes or first child
    const char* pos_str = elem.get_attr_string("pos");
    if (pos_str) {
        parse_coord_pair(pos_str, &x, &y);
    } else if (elem.has_attr("x") && elem.has_attr("y")) {
        x = (float)elem.get_int_attr("x", 0);
        y = (float)elem.get_int_attr("y", 0);
    }
    
    // Convert to document coordinates
    x *= state->unitlength;
    y *= state->unitlength;
    
    // Create a group with translation for the content
    Transform2D trans = Transform2D::translate(x, y);
    GraphicsElement* group = graphics_group(state->arena, &trans);
    
    // Process children of the put command
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (!child.isElement()) continue;
        
        ElementReader child_elem = child.asElement();
        const char* tag = child_elem.tagName();
        if (!tag) continue;
        
        GraphicsElement* gfx = nullptr;
        
        if (strcmp(tag, "line") == 0) {
            gfx = picture_cmd_line(state, child_elem);
        }
        else if (strcmp(tag, "vector") == 0) {
            gfx = picture_cmd_vector(state, child_elem);
        }
        else if (strcmp(tag, "circle") == 0) {
            gfx = picture_cmd_circle(state, child_elem);
        }
        else if (strcmp(tag, "oval") == 0) {
            gfx = picture_cmd_oval(state, child_elem);
        }
        else if (strcmp(tag, "qbezier") == 0) {
            gfx = picture_cmd_qbezier(state, child_elem);
        }
        else if (strcmp(tag, "framebox") == 0) {
            gfx = picture_cmd_framebox(state, child_elem);
        }
        else if (strcmp(tag, "makebox") == 0) {
            gfx = picture_cmd_makebox(state, child_elem);
        }
        else if (strcmp(tag, "dashbox") == 0) {
            gfx = picture_cmd_dashbox(state, child_elem);
        }
        else {
            // Possibly text content - treat as text element
            // TODO: Handle text content
            log_debug("picture_cmd_put: nested command '%s'", tag);
        }
        
        if (gfx) {
            graphics_append_child(group, gfx);
        }
    }
    
    // Append group to current context
    graphics_append_child(state->current_group, group);
    log_debug("picture_cmd_put: placed at (%.1f, %.1f)", x, y);
}

void picture_cmd_multiput(PictureState* state, const ElementReader& elem) {
    // TODO: Implement when element traversal is integrated
    (void)state;
    (void)elem;
    log_debug("picture_cmd_multiput: stub");
}

GraphicsElement* picture_cmd_line(PictureState* state, const ElementReader& elem) {
    // \line(dx,dy){length}
    // Slope (dx,dy) must be coprime integers from -6 to 6
    // Length is in unitlength units
    
    int dx = 1, dy = 0;
    float length = 10.0f;
    
    // Parse slope
    const char* slope_str = elem.get_attr_string("slope");
    if (slope_str) {
        parse_slope_pair(slope_str, &dx, &dy);
    } else {
        dx = elem.get_int_attr("dx", 1);
        dy = elem.get_int_attr("dy", 0);
    }
    
    // Parse length
    const char* len_str = elem.get_attr_string("length");
    if (len_str) {
        length = parse_picture_dimension(len_str, state->unitlength);
    } else if (elem.has_attr("len")) {
        length = (float)elem.get_int_attr("len", 10) * state->unitlength;
    }
    
    // Calculate end point based on slope and length
    // Length represents the horizontal component for non-vertical lines
    float x1 = 0, y1 = 0;
    float x2, y2;
    
    if (dx == 0) {
        // Vertical line
        x2 = 0;
        y2 = (dy > 0) ? length : -length;
    } else {
        // Use length as horizontal span
        x2 = (dx > 0) ? length : -length;
        y2 = x2 * ((float)dy / (float)dx);
    }
    
    GraphicsElement* line = graphics_line(state->arena, x1, y1, x2, y2);
    line->style.stroke_color = state->stroke_color;
    line->style.stroke_width = state->line_thickness;
    
    log_debug("picture_cmd_line: slope(%d,%d) len=%.1f -> (%.1f,%.1f)-(%.1f,%.1f)",
             dx, dy, length, x1, y1, x2, y2);
    
    return line;
}

GraphicsElement* picture_cmd_vector(PictureState* state, const ElementReader& elem) {
    // Vector is like line but with arrow
    GraphicsElement* line = picture_cmd_line(state, elem);
    if (line) {
        line->line.has_arrow = true;
    }
    return line;
}

GraphicsElement* picture_cmd_circle(PictureState* state, const ElementReader& elem) {
    // \circle{diameter} or \circle*{diameter}
    float diameter = 10.0f;
    bool filled = false;
    
    // Parse diameter
    const char* diam_str = elem.get_attr_string("diameter");
    if (diam_str) {
        diameter = parse_picture_dimension(diam_str, 1.0f);
    } else if (elem.has_attr("d")) {
        diameter = (float)elem.get_int_attr("d", 10);
    }
    
    // Check for filled (starred) variant
    filled = elem.has_attr("filled") || elem.has_attr("starred");
    
    float radius = (diameter / 2.0f) * state->unitlength;
    GraphicsElement* circle = graphics_circle(state->arena, 0, 0, radius, filled);
    
    if (filled) {
        circle->style.fill_color = state->stroke_color;
        circle->style.stroke_color = "none";
    } else {
        circle->style.stroke_color = state->stroke_color;
        circle->style.stroke_width = state->line_thickness;
        circle->style.fill_color = "none";
    }
    
    log_debug("picture_cmd_circle: diameter=%.1f filled=%d", diameter, filled);
    
    return circle;
}

GraphicsElement* picture_cmd_oval(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    // Oval as ellipse
    float rx = 5.0f * state->unitlength;
    float ry = 3.0f * state->unitlength;
    
    GraphicsElement* ellipse = graphics_ellipse(state->arena, 0, 0, rx, ry);
    ellipse->style.stroke_color = state->stroke_color;
    ellipse->style.stroke_width = state->line_thickness;
    
    return ellipse;
}

GraphicsElement* picture_cmd_qbezier(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    // Create a simple quadratic bezier as placeholder
    float x0 = 0, y0 = 0;
    float x1 = 5, y1 = 10;
    float x2 = 10, y2 = 0;
    
    GraphicsElement* bezier = graphics_qbezier(state->arena,
        x0 * state->unitlength, y0 * state->unitlength,
        x1 * state->unitlength, y1 * state->unitlength,
        x2 * state->unitlength, y2 * state->unitlength);
    bezier->style.stroke_color = state->stroke_color;
    bezier->style.stroke_width = state->line_thickness;
    
    return bezier;
}

GraphicsElement* picture_cmd_framebox(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    float width = 20.0f * state->unitlength;
    float height = 10.0f * state->unitlength;
    
    GraphicsElement* rect = graphics_rect(state->arena, 0, 0, width, height, 0, 0);
    rect->style.stroke_color = state->stroke_color;
    rect->style.stroke_width = state->line_thickness;
    rect->style.fill_color = "none";
    
    return rect;
}

GraphicsElement* picture_cmd_makebox(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    // Makebox is like framebox but invisible
    GraphicsElement* group = graphics_group(state->arena, nullptr);
    
    // TODO: Add text content when integration is complete
    
    return group;
}

GraphicsElement* picture_cmd_dashbox(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    // Dashbox is like framebox but with dashed lines
    float width = 20.0f * state->unitlength;
    float height = 10.0f * state->unitlength;
    
    GraphicsElement* rect = graphics_rect(state->arena, 0, 0, width, height, 0, 0);
    rect->style.stroke_color = state->stroke_color;
    rect->style.stroke_width = state->line_thickness;
    rect->style.stroke_dasharray = "3,2";  // Simple dash pattern
    rect->style.fill_color = "none";
    
    return rect;
}

} // namespace tex
