// tex_graphics_picture.hpp - Picture environment builder for LaTeX graphics
//
// Converts LaTeX picture environment commands to GraphicsElement IR.
//
// Reference: vibe/Latex_Typeset_Design5_Graphics.md

#ifndef TEX_GRAPHICS_PICTURE_HPP
#define TEX_GRAPHICS_PICTURE_HPP

#include "tex_graphics.hpp"
#include "../mark_reader.hpp"
#include "lib/arena.h"

namespace tex {

// Forward declarations
struct TexDocumentModel;

// ============================================================================
// Picture Environment State
// ============================================================================

// State for building a picture environment
struct PictureState {
    Arena* arena;
    TexDocumentModel* doc;
    
    // Current graphics being built
    GraphicsElement* canvas;
    GraphicsElement* current_group;  // Current group for appending children
    
    // Picture parameters
    float unitlength;       // \unitlength in pt (default 1pt)
    float line_thickness;   // Current line thickness in pt
    float thin_line;        // \thinlines thickness (0.4pt)
    float thick_line;       // \thicklines thickness (0.8pt)
    
    // Current position (for some commands)
    float current_x;
    float current_y;
    
    // Current color
    const char* stroke_color;
    const char* fill_color;
};

// Initialize picture state
void picture_state_init(PictureState* state, Arena* arena, TexDocumentModel* doc);

// ============================================================================
// Picture Builder API
// ============================================================================

// Build GraphicsElement from picture environment
// elem should be the parsed picture environment Element
GraphicsElement* graphics_build_picture(const ElementReader& elem, 
                                         Arena* arena, 
                                         TexDocumentModel* doc);

// ============================================================================
// Individual Command Handlers
// ============================================================================

// Process \put(x,y){content}
void picture_cmd_put(PictureState* state, const ElementReader& elem);

// Process \multiput(x,y)(dx,dy){n}{content}
void picture_cmd_multiput(PictureState* state, const ElementReader& elem);

// Process \line(dx,dy){length}
GraphicsElement* picture_cmd_line(PictureState* state, const ElementReader& elem);

// Process \vector(dx,dy){length}
GraphicsElement* picture_cmd_vector(PictureState* state, const ElementReader& elem);

// Process \circle{diameter} or \circle*{diameter}
GraphicsElement* picture_cmd_circle(PictureState* state, const ElementReader& elem);

// Process \oval(w,h)[portion]
GraphicsElement* picture_cmd_oval(PictureState* state, const ElementReader& elem);

// Process \qbezier[n](x0,y0)(x1,y1)(x2,y2)
GraphicsElement* picture_cmd_qbezier(PictureState* state, const ElementReader& elem);

// Process \framebox(w,h)[pos]{content}
GraphicsElement* picture_cmd_framebox(PictureState* state, const ElementReader& elem);

// Process \makebox(w,h)[pos]{content}
GraphicsElement* picture_cmd_makebox(PictureState* state, const ElementReader& elem);

// Process \dashbox{dash}(w,h)[pos]{content}
GraphicsElement* picture_cmd_dashbox(PictureState* state, const ElementReader& elem);

// ============================================================================
// Coordinate Parsing Helpers
// ============================================================================

// Parse a coordinate pair (x,y) from string, returns in picture units
bool parse_coord_pair(const char* str, float* x, float* y);

// Parse a dimension value (with optional unit), returns in pt
float parse_picture_dimension(const char* str, float unitlength);

// Parse slope pair (dx,dy) which must be integers -6 to 6
bool parse_slope_pair(const char* str, int* dx, int* dy);

} // namespace tex

#endif // TEX_GRAPHICS_PICTURE_HPP
