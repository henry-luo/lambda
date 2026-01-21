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
GraphicsElement* picture_cmd_frame(PictureState* state, const ElementReader& elem);

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
    
    // Use document's current unitlength if available (set by \setlength{\unitlength}{...})
    if (doc && doc->picture_unitlength > 0) {
        state.unitlength = doc->picture_unitlength;
        log_debug("graphics_build_picture: using document unitlength=%.2fpt", state.unitlength);
    }
    
    // Get picture size: default values
    float width = 100.0f;
    float height = 100.0f;
    float origin_x = 0;
    float origin_y = 0;
    
    // Try to extract size and offset from first text child: (width,height)(x0,y0)
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                // Parse (width,height)
                parse_coord_pair(text, &width, &height);
                
                // Look for optional (x0,y0) offset
                const char* p = text;
                while (*p && *p != ')') p++;
                if (*p == ')') p++;
                p = skip_ws(p);
                if (*p == '(') {
                    parse_coord_pair(p, &origin_x, &origin_y);
                }
                break;  // Found size info
            }
        } else if (child.isElement()) {
            // Check for nested paragraph that might contain the size
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "paragraph") == 0) {
                auto iter2 = child_elem.children();
                ItemReader grandchild;
                while (iter2.next(&grandchild)) {
                    if (grandchild.isString()) {
                        const char* text = grandchild.cstring();
                        if (text && text[0] == '(') {
                            parse_coord_pair(text, &width, &height);
                            
                            // Look for optional offset
                            const char* p = text;
                            while (*p && *p != ')') p++;
                            if (*p == ')') p++;
                            p = skip_ws(p);
                            if (*p == '(') {
                                parse_coord_pair(p, &origin_x, &origin_y);
                            }
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    
    // Try to override from element attributes (if present)
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
    if (elem.has_attr("offset")) {
        const char* offset_str = elem.get_attr_string("offset");
        if (offset_str) {
            parse_coord_pair(offset_str, &origin_x, &origin_y);
        }
    }
    
    // Convert to pt using unitlength
    width *= state.unitlength;
    height *= state.unitlength;
    origin_x *= state.unitlength;
    origin_y *= state.unitlength;
    
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
        else if (strcmp(tag, "vector") == 0) {
            // \vector(dx,dy){length} - same as line but with arrow
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
            
            // Create line element with arrow
            GraphicsElement* gfx = create_line_from_slope(state, dx, dy, length);
            if (gfx) {
                gfx->line.has_arrow = true;
                graphics_append_child(state->current_group, gfx);
            }
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
        else if (strcmp(tag, "frame") == 0) {
            GraphicsElement* gfx = picture_cmd_frame(state, child_elem);
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
        else if (strcmp(tag, "frame") == 0) {
            gfx = picture_cmd_frame(state, child_elem);
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
    // \multiput(x,y)(dx,dy){n}{content}
    // Places content n times starting at (x,y), each shifted by (dx,dy)
    
    float x = 0, y = 0;
    float dx = 0, dy = 0;
    int n = 1;
    
    // Parse positions and count from attributes
    if (elem.has_attr("x")) {
        x = (float)elem.get_int_attr("x", 0);
    }
    if (elem.has_attr("y")) {
        y = (float)elem.get_int_attr("y", 0);
    }
    if (elem.has_attr("dx")) {
        dx = (float)elem.get_int_attr("dx", 0);
    }
    if (elem.has_attr("dy")) {
        dy = (float)elem.get_int_attr("dy", 0);
    }
    if (elem.has_attr("n")) {
        n = elem.get_int_attr("n", 1);
    }
    
    // Try to parse from children (positions might be text nodes)
    auto iter = elem.children();
    ItemReader child;
    int coord_index = 0;
    const ElementReader* content_elem = nullptr;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                // Coordinate pair
                if (coord_index == 0) {
                    parse_coord_pair(text, &x, &y);
                    coord_index++;
                } else if (coord_index == 1) {
                    parse_coord_pair(text, &dx, &dy);
                    coord_index++;
                }
            } else if (text) {
                // Could be the count
                int val = atoi(text);
                if (val > 0) n = val;
            }
        } else if (child.isElement()) {
            ElementReader el = child.asElement();
            const char* tag = el.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                // This is either the count or the content
                const char* inner_text = extract_first_text(el);
                if (inner_text) {
                    int val = atoi(inner_text);
                    if (val > 0 && content_elem == nullptr) {
                        n = val;
                    }
                }
                // Store as potential content
                content_elem = &el;
            }
        }
    }
    
    // Limit n to prevent runaway
    if (n > 1000) n = 1000;
    if (n < 1) n = 1;
    
    log_debug("picture_cmd_multiput: start=(%.1f,%.1f) delta=(%.1f,%.1f) n=%d", x, y, dx, dy, n);
    
    // Generate n copies with translation
    for (int i = 0; i < n; i++) {
        float curr_x = (x + i * dx) * state->unitlength;
        float curr_y = (y + i * dy) * state->unitlength;
        
        Transform2D trans = Transform2D::translate(curr_x, curr_y);
        GraphicsElement* group = graphics_group(state->arena, &trans);
        
        // Process content if we have it
        if (content_elem) {
            GraphicsElement* content = process_put_content(state, *content_elem);
            if (content) {
                graphics_append_child(group, content);
            }
        }
        
        graphics_append_child(state->current_group, group);
    }
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
    // Vector is like line but with arrow at the end
    // Slopes are more restricted than \line: only -4..4
    GraphicsElement* line = picture_cmd_line(state, elem);
    if (line) {
        line->line.has_arrow = true;
        log_debug("picture_cmd_vector: converted line to vector");
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
    // \oval(width,height)[portion]
    // portion is optional: t, b, l, r, tl, tr, bl, br for partial ovals
    
    float width = 10.0f;
    float height = 6.0f;
    const char* portion = nullptr;
    
    // Parse width and height from attributes
    if (elem.has_attr("width")) {
        width = (float)elem.get_int_attr("width", 10);
    }
    if (elem.has_attr("height")) {
        height = (float)elem.get_int_attr("height", 6);
    }
    
    // Try to get size from text children (e.g., "(10,6)")
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                parse_coord_pair(text, &width, &height);
            } else if (text && (text[0] == '[' || text[0] == 't' || text[0] == 'b' || text[0] == 'l' || text[0] == 'r')) {
                // This is the portion specifier
                portion = text;
                if (portion[0] == '[') portion++;  // Skip bracket
            }
        }
    }
    
    // Check for portion attribute
    if (!portion && elem.has_attr("portion")) {
        portion = elem.get_attr_string("portion");
    }
    
    // Convert to pt
    float w = width * state->unitlength;
    float h = height * state->unitlength;
    
    GraphicsElement* result = nullptr;
    
    if (!portion || strlen(portion) == 0) {
        // Full oval - use rect with rounded corners (LaTeXML style)
        // rx = ry = min(width, height) / 2 makes circular ends
        float corner_radius = (w < h ? w : h) / 2.0f;
        result = graphics_rect(state->arena, -w/2, -h/2, w, h, corner_radius, corner_radius);
        result->style.stroke_color = state->stroke_color;
        result->style.stroke_width = state->line_thickness;
        result->style.fill_color = "none";
    } else {
        // Partial oval - draw using path
        // For simplicity, we approximate with quadratic beziers
        StrBuf* path = strbuf_new();
        
        // Oval radii for path calculations
        float rx = w / 2.0f;
        float ry = h / 2.0f;
        
        // Oval is centered at (0,0), going clockwise from right
        // Top half: from right (+rx, 0) to left (-rx, 0) via top (0, +ry)
        // Bottom half: from left (-rx, 0) to right (+rx, 0) via bottom (0, -ry)
        
        bool draw_top = false;
        bool draw_bottom = false;
        bool draw_left = false;
        bool draw_right = false;
        
        // Parse portion string
        for (const char* p = portion; *p; p++) {
            if (*p == 't') draw_top = true;
            if (*p == 'b') draw_bottom = true;
            if (*p == 'l') draw_left = true;
            if (*p == 'r') draw_right = true;
        }
        
        // If only corners specified, infer the edges
        if (!draw_top && !draw_bottom && !draw_left && !draw_right) {
            // Default to full oval if no valid portion
            draw_top = draw_bottom = draw_left = draw_right = true;
        }
        
        // Build path for the specified portions
        // Using cubic bezier approximation for quarter circles
        // For circle: control point distance = radius * 0.5523
        float kx = rx * 0.5523f;
        float ky = ry * 0.5523f;
        
        bool started = false;
        
        // Top-right quarter (from right to top)
        if ((draw_top && draw_right) || (draw_top || draw_right)) {
            if (!started) {
                strbuf_append_format(path, "M %.4f 0 ", rx);
                started = true;
            }
            strbuf_append_format(path, "C %.4f %.4f %.4f %.4f 0 %.4f ", rx, ky, kx, ry, ry);
        }
        
        // Top-left quarter (from top to left)
        if ((draw_top && draw_left) || (draw_top || draw_left)) {
            if (!started) {
                strbuf_append_format(path, "M 0 %.4f ", ry);
                started = true;
            }
            strbuf_append_format(path, "C %.4f %.4f %.4f %.4f %.4f 0 ", -kx, ry, -rx, ky, -rx);
        }
        
        // Bottom-left quarter (from left to bottom)
        if ((draw_bottom && draw_left) || (draw_bottom || draw_left)) {
            if (!started) {
                strbuf_append_format(path, "M %.4f 0 ", -rx);
                started = true;
            }
            strbuf_append_format(path, "C %.4f %.4f %.4f %.4f 0 %.4f ", -rx, -ky, -kx, -ry, -ry);
        }
        
        // Bottom-right quarter (from bottom to right)
        if ((draw_bottom && draw_right) || (draw_bottom || draw_right)) {
            if (!started) {
                strbuf_append_format(path, "M 0 %.4f ", -ry);
                started = true;
            }
            strbuf_append_format(path, "C %.4f %.4f %.4f %.4f %.4f 0 ", kx, -ry, rx, -ky, rx);
        }
        
        // Copy path to arena
        char* path_str = (char*)arena_alloc(state->arena, path->length + 1);
        memcpy(path_str, path->str, path->length);
        path_str[path->length] = '\0';
        strbuf_free(path);
        
        result = graphics_path(state->arena, path_str);
        result->style.stroke_color = state->stroke_color;
        result->style.stroke_width = state->line_thickness;
        result->style.fill_color = "none";
    }
    
    log_debug("picture_cmd_oval: size=%.1fx%.1f portion=%s", width, height, portion ? portion : "full");
    
    return result;
}

GraphicsElement* picture_cmd_qbezier(PictureState* state, const ElementReader& elem) {
    // \qbezier[n](x0,y0)(x1,y1)(x2,y2)
    // n is optional: number of points for approximation (ignored in SVG, which has native bezier)
    // (x0,y0) start point
    // (x1,y1) control point
    // (x2,y2) end point
    
    float x0 = 0, y0 = 0;
    float x1 = 5, y1 = 10;  // Default control point
    float x2 = 10, y2 = 0;
    
    // Parse from attributes
    if (elem.has_attr("x0")) {
        x0 = (float)elem.get_int_attr("x0", 0);
        y0 = (float)elem.get_int_attr("y0", 0);
    }
    if (elem.has_attr("x1")) {
        x1 = (float)elem.get_int_attr("x1", 5);
        y1 = (float)elem.get_int_attr("y1", 10);
    }
    if (elem.has_attr("x2")) {
        x2 = (float)elem.get_int_attr("x2", 10);
        y2 = (float)elem.get_int_attr("y2", 0);
    }
    
    // Try to parse from text children
    auto iter = elem.children();
    ItemReader child;
    int point_index = 0;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                float px, py;
                if (parse_coord_pair(text, &px, &py)) {
                    switch (point_index) {
                        case 0: x0 = px; y0 = py; break;
                        case 1: x1 = px; y1 = py; break;
                        case 2: x2 = px; y2 = py; break;
                    }
                    point_index++;
                }
            } else if (text && text[0] == '[') {
                // This is the optional [n] parameter - we ignore it
            }
        }
    }
    
    // Convert to pt
    GraphicsElement* bezier = graphics_qbezier(state->arena,
        x0 * state->unitlength, y0 * state->unitlength,
        x1 * state->unitlength, y1 * state->unitlength,
        x2 * state->unitlength, y2 * state->unitlength);
    bezier->style.stroke_color = state->stroke_color;
    bezier->style.stroke_width = state->line_thickness;
    bezier->style.fill_color = "none";
    
    log_debug("picture_cmd_qbezier: (%.1f,%.1f)-(%.1f,%.1f)-(%.1f,%.1f)", x0, y0, x1, y1, x2, y2);
    
    return bezier;
}

GraphicsElement* picture_cmd_framebox(PictureState* state, const ElementReader& elem) {
    // \framebox(width,height)[position]{content}
    // Draws a rectangular frame with optional text content
    
    float width = 20.0f;
    float height = 10.0f;
    const char* position = "c";  // Default center
    
    // Parse dimensions from attributes
    if (elem.has_attr("width")) {
        width = (float)elem.get_int_attr("width", 20);
    }
    if (elem.has_attr("height")) {
        height = (float)elem.get_int_attr("height", 10);
    }
    if (elem.has_attr("position")) {
        position = elem.get_attr_string("position");
    }
    
    // Try to parse from children
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                parse_coord_pair(text, &width, &height);
            } else if (text && text[0] == '[') {
                // Position specifier
                position = text + 1;  // Skip [
            }
        }
    }
    
    // Convert to pt
    float w = width * state->unitlength;
    float h = height * state->unitlength;
    
    // Create the box frame - centered at origin
    // The position affects where content goes, not the box itself
    GraphicsElement* rect = graphics_rect(state->arena, -w/2, -h/2, w, h, 0, 0);
    rect->style.stroke_color = state->stroke_color;
    rect->style.stroke_width = state->line_thickness;
    rect->style.fill_color = "none";
    
    log_debug("picture_cmd_framebox: size=%.1fx%.1f position=%s", width, height, position);
    
    return rect;
}

GraphicsElement* picture_cmd_makebox(PictureState* state, const ElementReader& elem) {
    // \makebox(width,height)[position]{content}
    // Like framebox but without the frame (invisible box)
    // Used for positioning text
    
    float width = 20.0f;
    float height = 10.0f;
    const char* position = "c";
    const char* text_content = nullptr;
    
    // Parse dimensions
    if (elem.has_attr("width")) {
        width = (float)elem.get_int_attr("width", 20);
    }
    if (elem.has_attr("height")) {
        height = (float)elem.get_int_attr("height", 10);
    }
    if (elem.has_attr("position")) {
        position = elem.get_attr_string("position");
    }
    
    // Try to parse from children
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                parse_coord_pair(text, &width, &height);
            } else if (text && text[0] == '[') {
                position = text + 1;
            } else if (text && text[0] != '\0') {
                text_content = text;
            }
        } else if (child.isElement()) {
            ElementReader el = child.asElement();
            const char* tag = el.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                // Extract text content
                text_content = extract_first_text(el);
            }
        }
    }
    
    // Create a group (invisible box)
    GraphicsElement* group = graphics_group(state->arena, nullptr);
    
    // If there's text content, add it
    if (text_content && strlen(text_content) > 0) {
        // Calculate text position based on alignment
        float tx = 0, ty = 0;
        const char* anchor = "middle";
        
        // Position parsing: l=left, r=right, t=top, b=bottom, c=center
        for (const char* p = position; p && *p; p++) {
            switch (*p) {
                case 'l': anchor = "start"; tx = -width/2 * state->unitlength; break;
                case 'r': anchor = "end"; tx = width/2 * state->unitlength; break;
                case 't': ty = height/2 * state->unitlength; break;
                case 'b': ty = -height/2 * state->unitlength; break;
            }
        }
        
        GraphicsElement* text = graphics_text(state->arena, tx, ty, text_content);
        text->text.anchor = anchor;
        text->style.fill_color = state->stroke_color;
        graphics_append_child(group, text);
    }
    
    log_debug("picture_cmd_makebox: size=%.1fx%.1f position=%s text=%s", 
              width, height, position, text_content ? text_content : "(none)");
    
    return group;
}

GraphicsElement* picture_cmd_dashbox(PictureState* state, const ElementReader& elem) {
    // \dashbox{dashlength}(width,height)[position]{content}
    // Like framebox but with dashed lines
    
    float dash_length = 3.0f;  // Default dash length
    float width = 20.0f;
    float height = 10.0f;
    const char* position = "c";
    
    // Parse attributes
    if (elem.has_attr("dash")) {
        dash_length = (float)elem.get_int_attr("dash", 3);
    }
    if (elem.has_attr("width")) {
        width = (float)elem.get_int_attr("width", 20);
    }
    if (elem.has_attr("height")) {
        height = (float)elem.get_int_attr("height", 10);
    }
    if (elem.has_attr("position")) {
        position = elem.get_attr_string("position");
    }
    
    // Try to parse from children
    auto iter = elem.children();
    ItemReader child;
    bool seen_size = false;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            const char* text = child.cstring();
            if (text && text[0] == '(') {
                parse_coord_pair(text, &width, &height);
                seen_size = true;
            } else if (text && text[0] == '[') {
                position = text + 1;
            } else if (text && text[0] == '{') {
                // Dash length in first curly group
                float val;
                if (sscanf(text, "{%f}", &val) == 1 && !seen_size) {
                    dash_length = val;
                }
            }
        }
    }
    
    float w = width * state->unitlength;
    float h = height * state->unitlength;
    float dl = dash_length * state->unitlength;
    
    // Create dashed rectangle
    GraphicsElement* rect = graphics_rect(state->arena, -w/2, -h/2, w, h, 0, 0);
    rect->style.stroke_color = state->stroke_color;
    rect->style.stroke_width = state->line_thickness;
    rect->style.fill_color = "none";
    
    // Format dash array
    char dash_str[32];
    snprintf(dash_str, sizeof(dash_str), "%.1f,%.1f", dl, dl);
    rect->style.stroke_dasharray = arena_strdup(state->arena, dash_str);
    
    log_debug("picture_cmd_dashbox: size=%.1fx%.1f dash=%.1f position=%s", 
              width, height, dash_length, position);
    
    return rect;
}

GraphicsElement* picture_cmd_frame(PictureState* state, const ElementReader& elem) {
    // \frame{content}
    // Draws a frame around content, auto-sizing to fit
    // Unlike framebox, frame doesn't take explicit size - it wraps content tightly
    
    // Estimate content size (for now, use a default small size)
    // In a full implementation, we would measure the content
    float content_width = 10.0f;  // Approximate width based on content
    float content_height = 8.0f;  // Approximate height based on content
    
    // Try to extract and estimate text content size
    const char* text_content = nullptr;
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isString()) {
            text_content = child.cstring();
            if (text_content) {
                // Rough estimate: ~6pt per character width, ~10pt height
                size_t len = strlen(text_content);
                content_width = (float)(len * 6);
                content_height = 10.0f;
            }
        } else if (child.isElement()) {
            ElementReader child_elem = child.asElement();
            const char* tag = child_elem.tagName();
            if (tag && strcmp(tag, "curly_group") == 0) {
                // Extract text from curly group
                auto group_iter = child_elem.children();
                ItemReader group_child;
                while (group_iter.next(&group_child)) {
                    if (group_child.isString()) {
                        text_content = group_child.cstring();
                        if (text_content) {
                            size_t len = strlen(text_content);
                            content_width = (float)(len * 6);
                            content_height = 10.0f;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    // Add small padding
    float padding = 2.0f;
    float w = content_width + 2 * padding;
    float h = content_height + 2 * padding;
    
    // Create rectangle at origin (will be positioned by put)
    GraphicsElement* rect = graphics_rect(state->arena, 0, 0, w, h, 0, 0);
    rect->style.stroke_color = state->stroke_color;
    rect->style.stroke_width = state->line_thickness;
    rect->style.fill_color = "none";
    
    log_debug("picture_cmd_frame: estimated size=%.1fx%.1f content='%s'", 
              w, h, text_content ? text_content : "(none)");
    
    return rect;
}

} // namespace tex
