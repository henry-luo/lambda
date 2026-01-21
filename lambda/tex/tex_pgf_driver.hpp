// tex_pgf_driver.hpp - PGF System Driver for TikZ Support
//
// This implements the PGF system layer (pgfsys-*) commands that build
// SVG output. TikZ/PGF macros expand to these low-level commands.
//
// The strategy follows LaTeXML's approach:
// 1. TikZ high-level commands expand via standard PGF macros
// 2. PGF basic layer commands expand to pgfsys@* commands  
// 3. We intercept pgfsys@* commands and build GraphicsElement IR
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md

#ifndef TEX_PGF_DRIVER_HPP
#define TEX_PGF_DRIVER_HPP

#include "tex_graphics.hpp"
#include "../mark_reader.hpp"
#include "lib/arena.h"
#include "lib/strbuf.h"
#include "lib/arraylist.h"

namespace tex {

// Forward declarations
struct TexDocumentModel;
struct DocElement;

// ============================================================================
// Color Representation
// ============================================================================

struct PgfColor {
    float r, g, b, a;  // 0-1 range
    
    static PgfColor black() { return {0, 0, 0, 1}; }
    static PgfColor white() { return {1, 1, 1, 1}; }
    static PgfColor none() { return {0, 0, 0, 0}; }  // a=0 means no color
    
    static PgfColor from_rgb(float r, float g, float b) {
        return {r, g, b, 1};
    }
    
    static PgfColor from_gray(float g) {
        return {g, g, g, 1};
    }
    
    static PgfColor from_cmyk(float c, float m, float y, float k) {
        float r = (1 - c) * (1 - k);
        float g = (1 - m) * (1 - k);
        float b = (1 - y) * (1 - k);
        return {r, g, b, 1};
    }
    
    // Convert to CSS color string (allocates from arena)
    const char* to_css(Arena* arena) const;
    
    bool is_none() const { return a == 0; }
};

// ============================================================================
// Graphics State
// ============================================================================

struct PgfGraphicsState {
    // Stroke properties
    PgfColor stroke_color;
    float line_width;           // in pt
    const char* dash_pattern;   // SVG dash-array string or nullptr
    float dash_offset;
    const char* line_cap;       // "butt", "round", "square"
    const char* line_join;      // "miter", "round", "bevel"
    float miter_limit;
    
    // Fill properties
    PgfColor fill_color;
    const char* fill_rule;      // "nonzero", "evenodd"
    
    // Transform
    Transform2D transform;
    
    // Opacity
    float stroke_opacity;
    float fill_opacity;
    
    // Default state
    static PgfGraphicsState defaults() {
        PgfGraphicsState s = {};
        s.stroke_color = PgfColor::black();
        s.line_width = 0.4f;
        s.dash_pattern = nullptr;
        s.dash_offset = 0;
        s.line_cap = nullptr;
        s.line_join = nullptr;
        s.miter_limit = 10;
        s.fill_color = PgfColor::none();
        s.fill_rule = "nonzero";
        s.transform = Transform2D::identity();
        s.stroke_opacity = 1;
        s.fill_opacity = 1;
        return s;
    }
    
    // Apply state to GraphicsStyle
    void apply_to_style(GraphicsStyle* style, Arena* arena) const;
};

// ============================================================================
// PGF Driver State
// ============================================================================

// Maximum scope nesting depth
constexpr int PGF_MAX_SCOPE_DEPTH = 64;

struct PgfDriverState {
    Arena* arena;
    TexDocumentModel* doc;
    
    // Path being built (SVG path data)
    StrBuf* path_data;
    bool path_started;
    float path_start_x, path_start_y;  // For closepath
    float path_cur_x, path_cur_y;      // Current point
    
    // Graphics state stack (for \pgfsys@beginscope / \pgfsys@endscope)
    PgfGraphicsState state_stack[PGF_MAX_SCOPE_DEPTH];
    int state_stack_top;
    
    // Output tree
    GraphicsElement* root;             // Root canvas
    GraphicsElement* current_group;    // Current group for output
    
    // Group stack (parallel to state stack)
    GraphicsElement* group_stack[PGF_MAX_SCOPE_DEPTH];
    int group_stack_top;
    
    // Clipping
    int clip_id_counter;
    bool clip_next;                    // Next path should be used for clipping
    
    // Picture dimensions
    float width, height;
    float origin_x, origin_y;
    
    // Accumulated definitions (markers, gradients, etc.)
    StrBuf* defs;
};

// ============================================================================
// Driver Initialization and Finalization
// ============================================================================

// Initialize PGF driver state
void pgf_driver_init(PgfDriverState* state, Arena* arena, TexDocumentModel* doc);

// Finalize and return the GraphicsElement tree
GraphicsElement* pgf_driver_finalize(PgfDriverState* state);

// Reset for new picture
void pgf_driver_reset(PgfDriverState* state);

// ============================================================================
// Current State Access
// ============================================================================

// Get current graphics state (top of stack)
PgfGraphicsState* pgf_current_state(PgfDriverState* state);

// Get current group for appending elements
GraphicsElement* pgf_current_group(PgfDriverState* state);

// ============================================================================
// Path Construction (pgfsys@moveto, pgfsys@lineto, etc.)
// ============================================================================

// Begin a new path (implicit - called automatically)
void pgf_path_begin(PgfDriverState* state);

// Close and clear path
void pgf_path_clear(PgfDriverState* state);

// Move to (M command)
void pgf_path_moveto(PgfDriverState* state, float x, float y);

// Line to (L command)
void pgf_path_lineto(PgfDriverState* state, float x, float y);

// Cubic bezier to (C command)
void pgf_path_curveto(PgfDriverState* state, 
                       float x1, float y1,
                       float x2, float y2,
                       float x3, float y3);

// Rectangle (separate path)
void pgf_path_rect(PgfDriverState* state, float x, float y, float w, float h);

// Close path (Z command)
void pgf_path_closepath(PgfDriverState* state);

// ============================================================================
// Path Operations (pgfsys@stroke, pgfsys@fill, etc.)
// ============================================================================

// Stroke current path
void pgf_path_stroke(PgfDriverState* state);

// Fill current path
void pgf_path_fill(PgfDriverState* state);

// Fill and stroke current path
void pgf_path_fillstroke(PgfDriverState* state);

// Discard current path
void pgf_path_discard(PgfDriverState* state);

// Use current path for clipping
void pgf_path_clip(PgfDriverState* state);

// Mark that next path should be used for clipping
void pgf_set_clipnext(PgfDriverState* state);

// ============================================================================
// Graphics State (pgfsys@setlinewidth, pgfsys@setcolor, etc.)
// ============================================================================

// Set line width
void pgf_set_linewidth(PgfDriverState* state, float width);

// Set dash pattern
void pgf_set_dash(PgfDriverState* state, const char* pattern, float offset);

// Set line cap
void pgf_set_linecap(PgfDriverState* state, int cap);  // 0=butt, 1=round, 2=square

// Set line join
void pgf_set_linejoin(PgfDriverState* state, int join);  // 0=miter, 1=round, 2=bevel

// Set miter limit
void pgf_set_miterlimit(PgfDriverState* state, float limit);

// Set stroke color (RGB)
void pgf_set_stroke_rgb(PgfDriverState* state, float r, float g, float b);

// Set fill color (RGB)
void pgf_set_fill_rgb(PgfDriverState* state, float r, float g, float b);

// Set stroke color (gray)
void pgf_set_stroke_gray(PgfDriverState* state, float g);

// Set fill color (gray)
void pgf_set_fill_gray(PgfDriverState* state, float g);

// Set stroke color (CMYK)
void pgf_set_stroke_cmyk(PgfDriverState* state, float c, float m, float y, float k);

// Set fill color (CMYK)
void pgf_set_fill_cmyk(PgfDriverState* state, float c, float m, float y, float k);

// Set opacity
void pgf_set_stroke_opacity(PgfDriverState* state, float opacity);
void pgf_set_fill_opacity(PgfDriverState* state, float opacity);

// ============================================================================
// Transformations (pgfsys@transformcm, etc.)
// ============================================================================

// Apply transformation matrix
void pgf_transform_cm(PgfDriverState* state,
                       float a, float b, float c, float d, float e, float f);

// Apply translation
void pgf_transform_shift(PgfDriverState* state, float x, float y);

// Apply scaling
void pgf_transform_scale(PgfDriverState* state, float sx, float sy);

// Apply rotation (degrees)
void pgf_transform_rotate(PgfDriverState* state, float degrees);

// ============================================================================
// Scoping (pgfsys@beginscope, pgfsys@endscope)
// ============================================================================

// Begin a new scope (saves graphics state)
void pgf_begin_scope(PgfDriverState* state);

// End scope (restores graphics state)
void pgf_end_scope(PgfDriverState* state);

// ============================================================================
// Special Operations
// ============================================================================

// Insert a text box (for \node content)
void pgf_hbox(PgfDriverState* state, float x, float y, DocElement* content);

// Insert raw SVG (for extensions)
void pgf_raw_svg(PgfDriverState* state, const char* svg);

// ============================================================================
// TikZ Picture Builder
// ============================================================================

// Build GraphicsElement from tikzpicture environment
GraphicsElement* graphics_build_tikz(const ElementReader& elem,
                                      Arena* arena,
                                      TexDocumentModel* doc);

} // namespace tex

#endif // TEX_PGF_DRIVER_HPP
