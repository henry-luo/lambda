// radiant/pdf/operators.h
// PDF operator parsing and graphics state management

#ifndef PDF_OPERATORS_H
#define PDF_OPERATORS_H

#include <stdint.h>
#include "../../lib/mempool.h"
#include "../../lib/stringbuf.h"
#include "../../lambda/lambda-data.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// PDF Graphics State Operators
typedef enum {
    // Text state operators
    PDF_OP_BT,          // Begin text
    PDF_OP_ET,          // End text
    PDF_OP_Tc,          // Character spacing
    PDF_OP_Tw,          // Word spacing
    PDF_OP_Tz,          // Horizontal scaling
    PDF_OP_TL,          // Leading
    PDF_OP_Tf,          // Font and size
    PDF_OP_Tr,          // Text rendering mode
    PDF_OP_Ts,          // Text rise

    // Text positioning operators
    PDF_OP_Td,          // Move text position
    PDF_OP_TD,          // Move text position and set leading
    PDF_OP_Tm,          // Set text matrix
    PDF_OP_T_star,      // Move to next line

    // Text showing operators
    PDF_OP_Tj,          // Show text
    PDF_OP_TJ,          // Show text with individual glyph positioning
    PDF_OP_quote,       // Move to next line and show text
    PDF_OP_dquote,      // Set spacing, move to next line, and show text

    // Graphics state operators
    PDF_OP_q,           // Save graphics state
    PDF_OP_Q,           // Restore graphics state
    PDF_OP_cm,          // Concatenate matrix to CTM

    // Color operators
    PDF_OP_CS,          // Set color space (stroking)
    PDF_OP_cs,          // Set color space (non-stroking)
    PDF_OP_SC,          // Set color (stroking)
    PDF_OP_sc,          // Set color (non-stroking)
    PDF_OP_SCN,         // Set color (stroking, with pattern)
    PDF_OP_scn,         // Set color (non-stroking, with pattern)
    PDF_OP_G,           // Set gray level (stroking)
    PDF_OP_g,           // Set gray level (non-stroking)
    PDF_OP_RG,          // Set RGB color (stroking)
    PDF_OP_rg,          // Set RGB color (non-stroking)

    // Line state operators
    PDF_OP_w,           // Set line width

    // Path construction operators
    PDF_OP_m,           // Move to
    PDF_OP_l,           // Line to
    PDF_OP_c,           // Cubic Bezier curve
    PDF_OP_v,           // Cubic Bezier curve (v1 = current point)
    PDF_OP_y,           // Cubic Bezier curve (v2 = v3)
    PDF_OP_h,           // Close path
    PDF_OP_re,          // Rectangle

    // Path painting operators
    PDF_OP_S,           // Stroke path
    PDF_OP_s,           // Close and stroke path
    PDF_OP_f,           // Fill path (nonzero winding)
    PDF_OP_F,           // Fill path (nonzero winding, obsolete)
    PDF_OP_f_star,      // Fill path (even-odd)
    PDF_OP_B,           // Fill and stroke (nonzero)
    PDF_OP_B_star,      // Fill and stroke (even-odd)
    PDF_OP_b,           // Close, fill and stroke (nonzero)
    PDF_OP_b_star,      // Close, fill and stroke (even-odd)
    PDF_OP_n,           // End path without filling or stroking

    // XObject operators
    PDF_OP_Do,          // Invoke named XObject

    PDF_OP_UNKNOWN
} PDFOperatorType;

// PDF Operator structure
typedef struct {
    PDFOperatorType type;
    const char* name;           // operator name (e.g., "Tj", "Tm")

    // Operands (varies by operator)
    union {
        struct {                // For Tj (show text)
            String* text;
        } show_text;

        struct {                // For Tf (set font)
            String* font_name;
            double size;
        } set_font;

        struct {                // For Tm (text matrix)
            double a, b, c, d, e, f;
        } text_matrix;

        struct {                // For Td (text position)
            double tx, ty;
        } text_position;

        struct {                // For rg/RG (RGB color)
            double r, g, b;
        } rgb_color;

        struct {                // For TJ (text array with positioning)
            Array* array;       // alternating strings and numbers
        } text_array;

        struct {                // For cm (transformation matrix)
            double a, b, c, d, e, f;
        } matrix;

        struct {                // For re (rectangle)
            double x, y, width, height;
        } rect;

        struct {                // For m/l (moveto/lineto - reusing text_position)
            double x, y;
        } point;

        struct {                // For c (cubic Bezier curve)
            double x1, y1, x2, y2, x3, y3;
        } curve;

        double number;          // For single number operands
    } operands;
} PDFOperator;

// Saved graphics state for q/Q operators
typedef struct PDFSavedState {
    double tm[6];              // Text matrix
    double tlm[6];             // Text line matrix
    double ctm[6];             // Current transformation matrix
    double char_spacing;
    double word_spacing;
    double horizontal_scaling;
    double leading;
    String* font_name;
    double font_size;
    int text_rendering_mode;
    double text_rise;
    double stroke_color[3];
    double fill_color[3];
    double line_width;
    double current_x;
    double current_y;
    struct PDFSavedState* next; // Stack link
} PDFSavedState;

// PDF Graphics State (maintains current state during parsing)
typedef struct {
    // Text state
    double char_spacing;        // Tc
    double word_spacing;        // Tw
    double horizontal_scaling;  // Tz (percent)
    double leading;            // TL
    String* font_name;         // Current font
    double font_size;          // Current font size
    int text_rendering_mode;   // Tr (0-7)
    double text_rise;          // Ts

    // Text matrix and line matrix
    double tm[6];              // Text matrix [a b c d e f]
    double tlm[6];             // Text line matrix

    // Current transformation matrix (CTM)
    double ctm[6];

    // Color state
    double stroke_color[3];    // RGB
    double fill_color[3];      // RGB

    // Line state
    double line_width;         // w operator (default 1.0)

    // Position tracking
    double current_x;
    double current_y;

    // State stack (for q/Q operators)
    PDFSavedState* saved_states; // Stack of saved states
    Pool* pool;                // Pool for allocations
} PDFGraphicsState;

// Parser context
typedef struct {
    const char* stream;        // Current position in stream
    const char* stream_end;    // End of stream
    Pool* pool;                // Memory pool
    PDFGraphicsState state;    // Current graphics state
    Input* input;              // Input context for string allocation
} PDFStreamParser;

// Function declarations

/**
 * Create a PDF stream parser
 */
PDFStreamParser* pdf_stream_parser_create(const char* stream, int length, Pool* pool, Input* input);

/**
 * Destroy a PDF stream parser
 */
void pdf_stream_parser_destroy(PDFStreamParser* parser);

/**
 * Parse the next operator from the stream
 * Returns NULL when no more operators
 */
PDFOperator* pdf_parse_next_operator(PDFStreamParser* parser);

/**
 * Initialize graphics state to default values
 */
void pdf_graphics_state_init(PDFGraphicsState* state, Pool* pool);

/**
 * Save current graphics state (q operator)
 */
void pdf_graphics_state_save(PDFGraphicsState* state);

/**
 * Restore saved graphics state (Q operator)
 */
void pdf_graphics_state_restore(PDFGraphicsState* state);

/**
 * Update text position based on Td/TD operators
 */
void pdf_update_text_position(PDFGraphicsState* state, double tx, double ty);

/**
 * Apply text matrix transformation
 */
void pdf_apply_text_matrix(PDFGraphicsState* state, double a, double b, double c, double d, double e, double f);

#ifdef __cplusplus
}
#endif

#endif // PDF_OPERATORS_H
