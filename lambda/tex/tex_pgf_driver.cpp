// tex_pgf_driver.cpp - PGF System Driver Implementation
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md
//
// NOTE: This is a functional implementation of the PGF driver state machine.
// The TikZ builder is a stub pending full integration with document model.

#include "tex_pgf_driver.hpp"
#include "tex_document_model.hpp"
#include "lib/log.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// Color Utilities
// ============================================================================

const char* PgfColor::to_css(Arena* arena) const {
    if (is_none()) {
        return "none";
    }
    
    char* buf = (char*)arena_alloc(arena, 10);
    int ri = (int)(r * 255 + 0.5f);
    int gi = (int)(g * 255 + 0.5f);
    int bi = (int)(b * 255 + 0.5f);
    
    ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
    gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
    bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);
    
    snprintf(buf, 10, "#%02X%02X%02X", ri, gi, bi);
    return buf;
}

void PgfGraphicsState::apply_to_style(GraphicsStyle* style, Arena* arena) const {
    style->stroke_color = stroke_color.to_css(arena);
    style->fill_color = fill_color.to_css(arena);
    style->stroke_width = line_width;
    style->stroke_dasharray = dash_pattern;
    style->stroke_linecap = line_cap;
    style->stroke_linejoin = line_join;
    style->miter_limit = miter_limit;
    
    if (stroke_opacity < 1 || fill_opacity < 1) {
        style->opacity = (stroke_opacity + fill_opacity) / 2;  // Approximate
    }
}

// ============================================================================
// Driver Initialization
// ============================================================================

void pgf_driver_init(PgfDriverState* state, Arena* arena, TexDocumentModel* doc) {
    state->arena = arena;
    state->doc = doc;
    
    state->path_data = strbuf_new();
    state->path_started = false;
    state->path_start_x = 0;
    state->path_start_y = 0;
    state->path_cur_x = 0;
    state->path_cur_y = 0;
    
    // Initialize state stack with defaults
    state->state_stack[0] = PgfGraphicsState::defaults();
    state->state_stack_top = 0;
    
    // Create root canvas (will be sized later)
    state->root = graphics_canvas(arena, 100, 100, 0, 0, 1.0f);
    state->current_group = state->root;
    
    state->group_stack[0] = state->root;
    state->group_stack_top = 0;
    
    state->clip_id_counter = 0;
    state->clip_next = false;
    
    state->width = 100;
    state->height = 100;
    state->origin_x = 0;
    state->origin_y = 0;
    
    state->defs = strbuf_new();
}

GraphicsElement* pgf_driver_finalize(PgfDriverState* state) {
    // Update canvas dimensions
    state->root->canvas.width = state->width;
    state->root->canvas.height = state->height;
    state->root->canvas.origin_x = state->origin_x;
    state->root->canvas.origin_y = state->origin_y;
    
    // Clean up
    strbuf_free(state->path_data);
    strbuf_free(state->defs);
    
    return state->root;
}

void pgf_driver_reset(PgfDriverState* state) {
    strbuf_reset(state->path_data);
    state->path_started = false;
    state->state_stack_top = 0;
    state->state_stack[0] = PgfGraphicsState::defaults();
    state->group_stack_top = 0;
    state->clip_next = false;
}

// ============================================================================
// State Access
// ============================================================================

PgfGraphicsState* pgf_current_state(PgfDriverState* state) {
    return &state->state_stack[state->state_stack_top];
}

GraphicsElement* pgf_current_group(PgfDriverState* state) {
    return state->group_stack[state->group_stack_top];
}

// ============================================================================
// Path Construction
// ============================================================================

void pgf_path_begin(PgfDriverState* state) {
    if (!state->path_started) {
        strbuf_reset(state->path_data);
        state->path_started = true;
    }
}

void pgf_path_clear(PgfDriverState* state) {
    strbuf_reset(state->path_data);
    state->path_started = false;
}

void pgf_path_moveto(PgfDriverState* state, float x, float y) {
    pgf_path_begin(state);
    
    strbuf_append_format(state->path_data, "M %.4f %.4f ", x, y);
    state->path_start_x = x;
    state->path_start_y = y;
    state->path_cur_x = x;
    state->path_cur_y = y;
}

void pgf_path_lineto(PgfDriverState* state, float x, float y) {
    pgf_path_begin(state);
    
    strbuf_append_format(state->path_data, "L %.4f %.4f ", x, y);
    state->path_cur_x = x;
    state->path_cur_y = y;
}

void pgf_path_curveto(PgfDriverState* state,
                       float x1, float y1,
                       float x2, float y2,
                       float x3, float y3) {
    pgf_path_begin(state);
    
    strbuf_append_format(state->path_data, "C %.4f %.4f %.4f %.4f %.4f %.4f ",
                         x1, y1, x2, y2, x3, y3);
    state->path_cur_x = x3;
    state->path_cur_y = y3;
}

void pgf_path_rect(PgfDriverState* state, float x, float y, float w, float h) {
    pgf_path_begin(state);
    
    strbuf_append_format(state->path_data, "M %.4f %.4f ", x, y);
    strbuf_append_format(state->path_data, "L %.4f %.4f ", x + w, y);
    strbuf_append_format(state->path_data, "L %.4f %.4f ", x + w, y + h);
    strbuf_append_format(state->path_data, "L %.4f %.4f ", x, y + h);
    strbuf_append_str(state->path_data, "Z ");
    
    state->path_cur_x = x;
    state->path_cur_y = y;
}

void pgf_path_closepath(PgfDriverState* state) {
    strbuf_append_str(state->path_data, "Z ");
    state->path_cur_x = state->path_start_x;
    state->path_cur_y = state->path_start_y;
}

// ============================================================================
// Path Operations
// ============================================================================

static void emit_path_element(PgfDriverState* state, bool stroke, bool fill) {
    if (!state->path_started || state->path_data->length == 0) {
        return;
    }
    
    PgfGraphicsState* gs = pgf_current_state(state);
    
    // Allocate path string from arena
    char* path_str = (char*)arena_alloc(state->arena, state->path_data->length + 1);
    memcpy(path_str, state->path_data->str, state->path_data->length);
    path_str[state->path_data->length] = '\0';
    
    GraphicsElement* elem = graphics_path(state->arena, path_str);
    
    // Apply current style
    if (stroke) {
        elem->style.stroke_color = gs->stroke_color.to_css(state->arena);
        elem->style.stroke_width = gs->line_width;
        elem->style.stroke_dasharray = gs->dash_pattern;
        elem->style.stroke_linecap = gs->line_cap;
        elem->style.stroke_linejoin = gs->line_join;
    } else {
        elem->style.stroke_color = "none";
    }
    
    if (fill) {
        elem->style.fill_color = gs->fill_color.to_css(state->arena);
    } else {
        elem->style.fill_color = "none";
    }
    
    // Apply current transform
    elem->transform = gs->transform;
    
    graphics_append_child(pgf_current_group(state), elem);
    pgf_path_clear(state);
}

void pgf_path_stroke(PgfDriverState* state) {
    emit_path_element(state, true, false);
}

void pgf_path_fill(PgfDriverState* state) {
    emit_path_element(state, false, true);
}

void pgf_path_fillstroke(PgfDriverState* state) {
    emit_path_element(state, true, true);
}

void pgf_path_discard(PgfDriverState* state) {
    pgf_path_clear(state);
}

void pgf_path_clip(PgfDriverState* state) {
    // For clipping, we'd create a clipPath element
    // For now, just discard the path
    pgf_path_discard(state);
    state->clip_next = false;
}

void pgf_set_clipnext(PgfDriverState* state) {
    state->clip_next = true;
}

// ============================================================================
// Graphics State
// ============================================================================

void pgf_set_linewidth(PgfDriverState* state, float width) {
    pgf_current_state(state)->line_width = width;
}

void pgf_set_dash(PgfDriverState* state, const char* pattern, float offset) {
    PgfGraphicsState* gs = pgf_current_state(state);
    gs->dash_pattern = pattern;
    gs->dash_offset = offset;
}

void pgf_set_linecap(PgfDriverState* state, int cap) {
    static const char* caps[] = {"butt", "round", "square"};
    if (cap >= 0 && cap <= 2) {
        pgf_current_state(state)->line_cap = caps[cap];
    }
}

void pgf_set_linejoin(PgfDriverState* state, int join) {
    static const char* joins[] = {"miter", "round", "bevel"};
    if (join >= 0 && join <= 2) {
        pgf_current_state(state)->line_join = joins[join];
    }
}

void pgf_set_miterlimit(PgfDriverState* state, float limit) {
    pgf_current_state(state)->miter_limit = limit;
}

void pgf_set_stroke_rgb(PgfDriverState* state, float r, float g, float b) {
    pgf_current_state(state)->stroke_color = PgfColor::from_rgb(r, g, b);
}

void pgf_set_fill_rgb(PgfDriverState* state, float r, float g, float b) {
    pgf_current_state(state)->fill_color = PgfColor::from_rgb(r, g, b);
}

void pgf_set_stroke_gray(PgfDriverState* state, float g) {
    pgf_current_state(state)->stroke_color = PgfColor::from_gray(g);
}

void pgf_set_fill_gray(PgfDriverState* state, float g) {
    pgf_current_state(state)->fill_color = PgfColor::from_gray(g);
}

void pgf_set_stroke_cmyk(PgfDriverState* state, float c, float m, float y, float k) {
    pgf_current_state(state)->stroke_color = PgfColor::from_cmyk(c, m, y, k);
}

void pgf_set_fill_cmyk(PgfDriverState* state, float c, float m, float y, float k) {
    pgf_current_state(state)->fill_color = PgfColor::from_cmyk(c, m, y, k);
}

void pgf_set_stroke_opacity(PgfDriverState* state, float opacity) {
    pgf_current_state(state)->stroke_opacity = opacity;
}

void pgf_set_fill_opacity(PgfDriverState* state, float opacity) {
    pgf_current_state(state)->fill_opacity = opacity;
}

// ============================================================================
// Transformations
// ============================================================================

void pgf_transform_cm(PgfDriverState* state,
                       float a, float b, float c, float d, float e, float f) {
    PgfGraphicsState* gs = pgf_current_state(state);
    Transform2D t = {a, b, c, d, e, f};
    gs->transform = gs->transform.multiply(t);
}

void pgf_transform_shift(PgfDriverState* state, float x, float y) {
    PgfGraphicsState* gs = pgf_current_state(state);
    Transform2D t = Transform2D::translate(x, y);
    gs->transform = gs->transform.multiply(t);
}

void pgf_transform_scale(PgfDriverState* state, float sx, float sy) {
    PgfGraphicsState* gs = pgf_current_state(state);
    Transform2D t = Transform2D::scale(sx, sy);
    gs->transform = gs->transform.multiply(t);
}

void pgf_transform_rotate(PgfDriverState* state, float degrees) {
    PgfGraphicsState* gs = pgf_current_state(state);
    Transform2D t = Transform2D::rotate(degrees);
    gs->transform = gs->transform.multiply(t);
}

// ============================================================================
// Scoping
// ============================================================================

void pgf_begin_scope(PgfDriverState* state) {
    if (state->state_stack_top >= PGF_MAX_SCOPE_DEPTH - 1) {
        log_error("pgf_begin_scope: scope stack overflow");
        return;
    }
    
    // Copy current state to next level
    state->state_stack[state->state_stack_top + 1] = state->state_stack[state->state_stack_top];
    state->state_stack_top++;
    
    // Create a new group
    GraphicsElement* group = graphics_group(state->arena, nullptr);
    graphics_append_child(pgf_current_group(state), group);
    
    if (state->group_stack_top < PGF_MAX_SCOPE_DEPTH - 1) {
        state->group_stack_top++;
        state->group_stack[state->group_stack_top] = group;
    }
}

void pgf_end_scope(PgfDriverState* state) {
    if (state->state_stack_top <= 0) {
        log_error("pgf_end_scope: scope stack underflow");
        return;
    }
    
    state->state_stack_top--;
    
    if (state->group_stack_top > 0) {
        state->group_stack_top--;
    }
}

// ============================================================================
// Special Operations
// ============================================================================

void pgf_hbox(PgfDriverState* state, float x, float y, DocElement* content) {
    // Create a text element with foreignObject content
    GraphicsElement* text = graphics_text(state->arena, x, y, nullptr);
    text->text.rich_content = content;
    
    graphics_append_child(pgf_current_group(state), text);
}

void pgf_raw_svg(PgfDriverState* state, const char* svg) {
    // Create a path element with raw SVG data
    // This is a hack - ideally we'd parse and integrate the SVG
    GraphicsElement* path = graphics_path(state->arena, svg);
    graphics_append_child(pgf_current_group(state), path);
}

// ============================================================================
// TikZ Picture Builder
// ============================================================================

// Helper to check if element has embedded SVG (LaTeXML case)
static bool has_embedded_svg(const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader el = child.asElement();
            const char* tag = el.tagName();
            // LaTeXML uses svg:svg namespace prefix
            if (tag && (strcmp(tag, "svg") == 0 || strcmp(tag, "svg:svg") == 0)) {
                return true;
            }
        }
    }
    return false;
}

// Extract embedded SVG content from element
static const char* extract_svg_content(const ElementReader& elem, Arena* arena) {
    auto iter = elem.children();
    ItemReader child;
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader el = child.asElement();
            const char* tag = el.tagName();
            if (tag && (strcmp(tag, "svg") == 0 || strcmp(tag, "svg:svg") == 0)) {
                // Get the raw SVG string - we need to serialize this element
                // For now, just note that we found SVG
                log_debug("graphics_build_tikz: found embedded SVG element");
                
                // Return a marker indicating SVG was found
                // The actual SVG extraction would need serialization support
                return "embedded";
            }
        }
    }
    return nullptr;
}

// Process TikZ/PGF commands from element children
static void process_tikz_children(PgfDriverState* state, const ElementReader& elem) {
    auto iter = elem.children();
    ItemReader child;
    
    while (iter.next(&child)) {
        if (child.isElement()) {
            ElementReader el = child.asElement();
            const char* tag = el.tagName();
            if (!tag) continue;
            
            // TikZ drawing commands
            if (strcmp(tag, "draw") == 0 || strcmp(tag, "tikz_draw") == 0) {
                // Process \draw command
                log_debug("process_tikz_children: found draw command");
                // Would parse path specifications, options, etc.
            }
            else if (strcmp(tag, "node") == 0 || strcmp(tag, "tikz_node") == 0) {
                // Process \node command
                log_debug("process_tikz_children: found node command");
                // Would parse position, content, options
            }
            else if (strcmp(tag, "path") == 0 || strcmp(tag, "tikz_path") == 0) {
                // Process \path command
                log_debug("process_tikz_children: found path command");
            }
            else if (strcmp(tag, "fill") == 0 || strcmp(tag, "tikz_fill") == 0) {
                // Process \fill command
                log_debug("process_tikz_children: found fill command");
            }
            else if (strcmp(tag, "filldraw") == 0 || strcmp(tag, "tikz_filldraw") == 0) {
                // Process \filldraw command
                log_debug("process_tikz_children: found filldraw command");
            }
            else if (strcmp(tag, "scope") == 0 || strcmp(tag, "tikz_scope") == 0) {
                // Process scope - creates a nested group
                pgf_begin_scope(state);
                process_tikz_children(state, el);
                pgf_end_scope(state);
            }
            else if (strcmp(tag, "coordinate") == 0 || strcmp(tag, "tikz_coordinate") == 0) {
                // Named coordinate definition
                log_debug("process_tikz_children: found coordinate definition");
            }
            else if (strcmp(tag, "foreach") == 0 || strcmp(tag, "tikz_foreach") == 0) {
                // \foreach loop - would need to expand iterations
                log_debug("process_tikz_children: found foreach (not yet expanded)");
            }
        }
    }
}

GraphicsElement* graphics_build_tikz(const ElementReader& elem,
                                      Arena* arena,
                                      TexDocumentModel* doc) {
    PgfDriverState state;
    pgf_driver_init(&state, arena, doc);
    
    // Read size from attributes (LaTeXML provides these)
    if (elem.has_attr("width")) {
        const char* w = elem.get_attr_string("width");
        if (w) {
            float val;
            if (sscanf(w, "%f", &val) == 1) {
                state.width = val;
            }
        }
    }
    if (elem.has_attr("height")) {
        const char* h = elem.get_attr_string("height");
        if (h) {
            float val;
            if (sscanf(h, "%f", &val) == 1) {
                state.height = val;
            }
        }
    }
    
    // Check for embedded SVG (LaTeXML case)
    // When TikZ is pre-rendered by LaTeXML, the SVG is already in the element
    if (has_embedded_svg(elem)) {
        log_debug("graphics_build_tikz: using pre-rendered SVG from LaTeXML");
        
        // Mark canvas as having embedded SVG
        // The SVG output generator will use the original SVG
        GraphicsElement* canvas = pgf_driver_finalize(&state);
        canvas->canvas.has_embedded_svg = true;
        
        // Store reference to original element for SVG extraction during output
        // This requires the output stage to handle embedded SVG specially
        return canvas;
    }
    
    // Process TikZ content from tree-sitter parsed elements
    // This handles native LaTeX parsing (not LaTeXML pre-rendered)
    process_tikz_children(&state, elem);
    
    log_debug("graphics_build_tikz: built canvas %.1fx%.1f", state.width, state.height);
    
    return pgf_driver_finalize(&state);
}

} // namespace tex
