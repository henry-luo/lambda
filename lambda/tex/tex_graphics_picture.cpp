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
    
    // TODO: Process child elements when integration is complete
    // For now, just return the empty canvas
    log_debug("graphics_build_picture: created canvas %.1fx%.1f", width, height);
    
    return state.canvas;
}

// ============================================================================
// Command Handlers (Stub implementations)
// ============================================================================

void picture_cmd_put(PictureState* state, const ElementReader& elem) {
    // TODO: Implement when element traversal is integrated
    (void)state;
    (void)elem;
    log_debug("picture_cmd_put: stub");
}

void picture_cmd_multiput(PictureState* state, const ElementReader& elem) {
    // TODO: Implement when element traversal is integrated
    (void)state;
    (void)elem;
    log_debug("picture_cmd_multiput: stub");
}

GraphicsElement* picture_cmd_line(PictureState* state, const ElementReader& elem) {
    (void)elem;
    
    // Create a simple line as placeholder
    float x1 = 0, y1 = 0, x2 = 10, y2 = 10;
    
    GraphicsElement* line = graphics_line(state->arena, x1, y1, x2, y2);
    line->style.stroke_color = state->stroke_color;
    line->style.stroke_width = state->line_thickness;
    
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
    (void)elem;
    
    float diameter = 10.0f;
    bool filled = false;
    
    float radius = diameter / 2.0f * state->unitlength;
    GraphicsElement* circle = graphics_circle(state->arena, 0, 0, radius, filled);
    circle->style.stroke_color = state->stroke_color;
    circle->style.stroke_width = state->line_thickness;
    
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
