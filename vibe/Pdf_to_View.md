# PDF to Radiant View Rendering Pipeline Implementation Plan

## Prelude: Approach Analysis and Design Decision

### Three Candidate Approaches

When considering how to render PDFs in Lambda's Radiant engine, three distinct approaches emerged:

#### Option 1: Direct PDF → Radiant View Tree

**Flow**: PDF Parser → PDF Operators → Radiant View Tree → Render (SVG/PDF/Canvas)

**Pros**:
- ✅ **Maximum Fidelity**: Direct mapping preserves exact PDF positioning and appearance
- ✅ **Performance**: Fewer transformation layers, less overhead
- ✅ **Simplicity**: Single conversion step, no intermediate format
- ✅ **Proven Pattern**: PDF.js uses this approach (PDF → Canvas operations)
- ✅ **Coordinate Precision**: Absolute positioning from PDF maps directly to view coordinates
- ✅ **Leverage Existing**: Uses Radiant's mature rendering pipeline

**Cons**:
- ⚠️ **Implementation Effort**: Requires PDF operator parser from scratch
- ⚠️ **No Reflow**: Fixed positioning makes reflowing text difficult
- ⚠️ **Custom Logic**: PDF-specific transformation and state management needed

#### Option 2: PDF → HTML/CSS → Radiant View Tree

**Flow**: PDF Parser → HTML Generator → CSS Styler → Radiant Layout → View Tree → Render

**Pros**:
- ✅ **Reuses Existing**: Leverages Radiant's complete HTML/CSS pipeline
- ✅ **Reflowable**: CSS enables responsive layouts and text reflow
- ✅ **Semantic**: HTML provides document structure
- ✅ **Editable**: Can modify content via CSS/HTML

**Cons**:
- ❌ **Complexity**: Adding another conversion layer (PDF → HTML)
- ❌ **Fidelity Loss**: CSS positioning may not exactly match PDF
- ❌ **Performance**: Multiple transformation steps
- ❌ **CSS Limitations**: Some PDF features hard to express in CSS
- ❌ **Maintenance**: Two systems to maintain (PDF→HTML + HTML→Views)

#### Option 3: PDF → SVG → ThorVG Rendering

**Flow**: PDF Parser → SVG Generator → ThorVG → Render

**Pros**:
- ✅ **Vector Format**: SVG naturally represents vector graphics
- ✅ **ThorVG Integration**: Already have ThorVG in Radiant
- ✅ **Visual Fidelity**: SVG can represent complex graphics well

**Cons**:
- ❌ **Loss of Semantics**: SVG is purely visual, no document structure
- ❌ **No Text Selection**: Text becomes graphics, hard to select/search
- ❌ **File Size**: SVG can be much larger than source PDF
- ❌ **No Interactivity**: Can't leverage Radiant's UI features
- ❌ **Limited Editing**: SVG text editing is problematic

### Decision: Option 1 (Direct PDF → View Tree)

**We chose Option 1** based on these key factors:

1. **Proven Architecture**: PDF.js's success with direct canvas rendering validates this approach
2. **Fidelity Requirements**: PDFs demand pixel-perfect rendering
3. **Performance**: Minimizes overhead with direct conversion
4. **Reusability**: Radiant's rendering pipeline handles output formats (SVG, PDF, Canvas, Screen)
5. **Incremental Path**: Can add text selection layer later (like PDF.js does)

### Comparison: Our Flow vs. PDF.js

#### PDF.js Architecture
```
PDF Input
    ↓
PDF Parser (core/parser.js)
    ↓
Content Stream Evaluator (core/evaluator.js)
    ↓ (processes operators)
    ↓
┌───────────────────┬──────────────────────┐
↓                   ↓                      ↓
Canvas Graphics   Text Layer (HTML)   Annotation Layer
(display/canvas.js) (display/text_layer.js) (display/annotation.js)
    ↓                   ↓                      ↓
HTML5 Canvas      Invisible HTML Divs    Interactive Elements
```

**Key PDF.js Features**:
- Direct operator-to-canvas rendering
- Separate text layer for selection (transparent HTML divs)
- Graphics state management (fonts, colors, transforms)
- Lazy loading and incremental rendering

#### Our Lambda/Radiant Architecture
```
PDF Input
    ↓
PDF Parser (lambda/input/input-pdf.cpp)
    ↓
Content Stream Processor (radiant/pdf/operators.cpp)
    ↓ (processes operators)
    ↓
Radiant View Tree Generator (radiant/pdf/pdf_to_view.cpp)
    ↓ (ViewBlock, ViewText, ViewSpan)
    ↓
Radiant Layout Engine (radiant/layout*.cpp)
    ↓ (optional adjustments)
    ↓
Radiant Renderer (radiant/render*.cpp)
    ↓
┌─────────┬─────────┬─────────┬─────────┐
↓         ↓         ↓         ↓         ↓
Canvas    SVG       PDF       Screen   (Text Layer)
                              (GLFW)   (Optional HTML)
```

**Key Differences**:
- **Abstraction Level**: We generate view tree vs. direct canvas operations
- **Rendering Flexibility**: Our view tree can output to multiple formats (SVG, PDF, Canvas, Screen)
- **Layout Engine**: We can optionally use Radiant's layout for adjustments
- **Memory Model**: Pool-based allocation vs. JavaScript GC
- **Integration**: Tighter integration with Lambda's data processing pipeline

**Similarities**:
- ✅ Direct operator parsing (no intermediate format)
- ✅ Graphics state management
- ✅ Coordinate transformations
- ✅ Optional text selection layer
- ✅ Font mapping and handling
- ✅ Incremental/lazy loading support

### Why This Works for Lambda

1. **Unified Pipeline**: PDF becomes just another input format alongside HTML, Markdown, JSON
2. **Output Flexibility**: Same view tree can render to SVG (for web), PDF (for print), or Canvas (for UI)
3. **Data Processing**: Lambda's data transformation capabilities can manipulate the view tree
4. **Performance**: Pool allocation and direct conversion minimize overhead
5. **Future-Proof**: Can add features incrementally (compression, images, forms) without redesigning

## Executive Summary

This document outlines an incremental implementation plan for rendering PDF documents using Lambda's Radiant layout engine. The design is inspired by Mozilla's PDF.js architecture, which uses a dual-layer approach: direct canvas rendering for visual fidelity and an invisible HTML text layer for selection/accessibility.

**Key Design Decision**: Direct PDF → Radiant View Tree conversion (no intermediate HTML/CSS), following PDF.js's proven approach of maintaining fidelity through direct rendering.

## Architecture Overview

```
PDF Input (binary)
    ↓
PDF Parser (existing: lambda/input/input-pdf.cpp)
    ↓
PDF Data Structure (Map with objects, streams, fonts)
    ↓
PDF Content Stream Processor (NEW)
    ↓ (parse operators: BT/ET, Tj/TJ, etc.)
    ↓
Radiant View Tree Generator (NEW: radiant/pdf/pdf_to_view.cpp)
    ↓ (ViewBlock, ViewText, ViewSpan with absolute positioning)
    ↓
Radiant Layout Engine (existing: radiant/layout*.cpp)
    ↓ (optional reflow/adjustment)
    ↓
Radiant Renderer (existing)
    ↓
Output: SVG, PDF, Canvas, Screen
```

### Key Components

1. **PDF Content Stream Processor**: Parses PDF operator sequences (Tj, TJ, Tm, etc.)
2. **View Tree Generator**: Maps PDF operations to Radiant view nodes
3. **Font Mapper**: Converts PDF fonts to system fonts
4. **Coordinate Transformer**: Handles PDF→Radiant coordinate conversion
5. **Text Selection Layer** (optional): Transparent HTML overlay for text selection

## Phase 1: Foundation (Week 1-2)

### Goal
Establish basic PDF operator parsing and simple text rendering.

### Deliverables

#### 1.1 PDF Operator Parser (`radiant/pdf/operators.h`, `radiant/pdf/operators.cpp`)

```cpp
// radiant/pdf/operators.h
#ifndef PDF_OPERATORS_H
#define PDF_OPERATORS_H

#include <stdint.h>
#include "../lib/mempool.h"

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

        double number;          // For single number operands
    } operands;
} PDFOperator;

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

    // Position tracking
    double current_x;
    double current_y;

    // State stack (for q/Q operators)
    void* saved_states;        // Stack of saved states
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
PDFStreamParser* pdf_stream_parser_create(const char* stream, int length, Pool* pool, Input* input);
void pdf_stream_parser_destroy(PDFStreamParser* parser);
PDFOperator* pdf_parse_next_operator(PDFStreamParser* parser);
void pdf_graphics_state_init(PDFGraphicsState* state);
void pdf_graphics_state_save(PDFGraphicsState* state, Pool* pool);
void pdf_graphics_state_restore(PDFGraphicsState* state, Pool* pool);

#endif // PDF_OPERATORS_H
```

**Implementation Notes**:
- Start with text operators (BT, ET, Tj, TJ, Tm, Tf) only
- Add graphics operators (paths, colors) in Phase 2
- Use pool allocation for all structures
- Maintain graphics state stack for q/Q operators

#### 1.2 View Tree Generator Core (`radiant/pdf/pdf_to_view.cpp`)

```cpp
// radiant/pdf/pdf_to_view.cpp
#include "pdf_to_view.hpp"
#include "view.hpp"
#include "operators.h"
#include "../lambda/input/input.h"
#include "../lib/log.h"

/**
 * Convert PDF data to Radiant View Tree
 *
 * This is the main entry point for PDF rendering.
 * Takes parsed PDF data and generates a view tree suitable for Radiant layout.
 */
ViewTree* pdf_to_view_tree(Input* input, Item pdf_root) {
    log_info("Starting PDF to View Tree conversion");

    if (pdf_root.item == ITEM_NULL || pdf_root.item == ITEM_ERROR) {
        log_error("Invalid PDF data");
        return nullptr;
    }

    Map* pdf_data = (Map*)pdf_root.item;

    // Create view tree
    ViewTree* view_tree = (ViewTree*)pool_calloc(input->pool, sizeof(ViewTree));
    if (!view_tree) {
        log_error("Failed to allocate view tree");
        return nullptr;
    }

    view_tree->pool = input->pool;
    view_tree->html_version = 5; // Treat as HTML5 for layout purposes

    // Extract PDF version and statistics
    String* version_key = create_string_from_literal(input->pool, "version");
    Item version_item = map_get(pdf_data, version_key);
    if (version_item.item != ITEM_NULL) {
        String* version = (String*)version_item.item;
        log_info("PDF version: %s", version->chars);
    }

    // Get objects array
    String* objects_key = create_string_from_literal(input->pool, "objects");
    Item objects_item = map_get(pdf_data, objects_key);

    if (objects_item.item == ITEM_NULL) {
        log_warn("No objects found in PDF");
        return view_tree;
    }

    Array* objects = (Array*)objects_item.item;
    log_info("Processing %d PDF objects", objects->length);

    // Create root view (represents the document)
    ViewBlock* root_view = create_document_view(input->pool);
    view_tree->root = (View*)root_view;

    // Process each object looking for content streams
    for (int i = 0; i < objects->length; i++) {
        Item obj_item = array_get(objects, i);
        process_pdf_object(input, root_view, obj_item);
    }

    log_info("PDF to View Tree conversion complete");
    return view_tree;
}

/**
 * Create root document view
 */
static ViewBlock* create_document_view(Pool* pool) {
    ViewBlock* root = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
    root->type = RDT_VIEW_BLOCK;
    root->x = 0;
    root->y = 0;
    root->width = 612;  // Default letter width in points
    root->height = 792; // Default letter height in points
    return root;
}

/**
 * Process a single PDF object
 */
static void process_pdf_object(Input* input, ViewBlock* parent, Item obj_item) {
    if (obj_item.item == ITEM_NULL) return;

    // Check if this is a map (could be a stream or indirect object)
    if (!is_map(obj_item)) return;

    Map* obj_map = (Map*)obj_item.item;

    // Check for type field
    String* type_key = create_string_from_literal(input->pool, "type");
    Item type_item = map_get(obj_map, type_key);

    if (type_item.item == ITEM_NULL) return;

    String* type_str = (String*)type_item.item;

    // Process stream objects
    if (strcmp(type_str->chars, "stream") == 0) {
        process_pdf_stream(input, parent, obj_map);
    }
    // Process indirect objects
    else if (strcmp(type_str->chars, "indirect_object") == 0) {
        String* content_key = create_string_from_literal(input->pool, "content");
        Item content_item = map_get(obj_map, content_key);
        if (content_item.item != ITEM_NULL) {
            process_pdf_object(input, parent, content_item);
        }
    }
}

/**
 * Process a PDF content stream
 */
static void process_pdf_stream(Input* input, ViewBlock* parent, Map* stream_map) {
    log_debug("Processing PDF stream");

    // Get stream data
    String* data_key = create_string_from_literal(input->pool, "data");
    Item data_item = map_get(stream_map, data_key);

    if (data_item.item == ITEM_NULL) {
        log_warn("Stream has no data");
        return;
    }

    String* stream_data = (String*)data_item.item;

    // Get stream dictionary (contains Length, Filter, etc.)
    String* dict_key = create_string_from_literal(input->pool, "dictionary");
    Item dict_item = map_get(stream_map, dict_key);
    Map* stream_dict = (dict_item.item != ITEM_NULL) ? (Map*)dict_item.item : nullptr;

    // Check if stream is compressed
    bool is_compressed = false;
    if (stream_dict) {
        String* filter_key = create_string_from_literal(input->pool, "Filter");
        Item filter_item = map_get(stream_dict, filter_key);
        if (filter_item.item != ITEM_NULL) {
            is_compressed = true;
            log_warn("Compressed streams not yet supported (Phase 2)");
            return;
        }
    }

    // Parse the content stream
    PDFStreamParser* parser = pdf_stream_parser_create(
        stream_data->chars,
        stream_data->len,
        input->pool,
        input
    );

    if (!parser) {
        log_error("Failed to create stream parser");
        return;
    }

    // Process operators
    PDFOperator* op;
    while ((op = pdf_parse_next_operator(parser)) != nullptr) {
        process_pdf_operator(input, parent, parser, op);
    }

    pdf_stream_parser_destroy(parser);
}

/**
 * Process a single PDF operator
 */
static void process_pdf_operator(Input* input, ViewBlock* parent,
                                 PDFStreamParser* parser, PDFOperator* op) {
    switch (op->type) {
        case PDF_OP_BT:
            // Begin text - reset text matrix
            log_debug("Begin text");
            break;

        case PDF_OP_ET:
            // End text
            log_debug("End text");
            break;

        case PDF_OP_Tf:
            // Set font and size
            log_debug("Set font: %s, size: %.2f",
                     op->operands.set_font.font_name->chars,
                     op->operands.set_font.size);
            parser->state.font_name = op->operands.set_font.font_name;
            parser->state.font_size = op->operands.set_font.size;
            break;

        case PDF_OP_Tm:
            // Set text matrix
            log_debug("Set text matrix");
            memcpy(parser->state.tm, &op->operands.text_matrix, sizeof(double) * 6);
            memcpy(parser->state.tlm, &op->operands.text_matrix, sizeof(double) * 6);
            break;

        case PDF_OP_Td:
            // Move text position
            log_debug("Move text position: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            update_text_position(parser,
                               op->operands.text_position.tx,
                               op->operands.text_position.ty);
            break;

        case PDF_OP_Tj:
            // Show text - create ViewText
            log_debug("Show text: %s", op->operands.show_text.text->chars);
            create_text_view(input, parent, parser, op->operands.show_text.text);
            break;

        case PDF_OP_TJ:
            // Show text array (with kerning adjustments)
            log_debug("Show text array");
            create_text_array_views(input, parent, parser, op->operands.text_array.array);
            break;

        default:
            log_debug("Unhandled operator type: %d", op->type);
            break;
    }
}
```

#### 1.3 Initial Text View Creation

```cpp
/**
 * Create a ViewText node from PDF text
 */
static void create_text_view(Input* input, ViewBlock* parent,
                            PDFStreamParser* parser, String* text) {
    if (!text || text->len == 0) return;

    // Calculate position from text matrix
    double x = parser->state.tm[4];  // e component
    double y = parser->state.tm[5];  // f component

    // Convert PDF coordinates (bottom-left origin) to Radiant coordinates (top-left origin)
    // PDF y increases upward, Radiant y increases downward
    double radiant_y = parent->height - y;

    // Create ViewText
    ViewText* text_view = (ViewText*)pool_calloc(input->pool, sizeof(ViewText));
    text_view->type = RDT_VIEW_TEXT;
    text_view->x = x;
    text_view->y = radiant_y;

    // Create text node
    DomNode* text_node = (DomNode*)pool_calloc(input->pool, sizeof(DomNode));
    text_node->type = DOM_TEXT_NODE;
    // Store text content (needs proper text_data setup)

    text_view->node = text_node;

    // Add to parent
    append_child_view((View*)parent, (View*)text_view);

    log_debug("Created text view at (%.2f, %.2f): %s", x, radiant_y, text->chars);
}
```

### Testing Strategy

1. **Unit Tests**: Create `test/test_pdf_operators.c`
   - Test operator parsing
   - Test graphics state management
   - Test coordinate transformations

2. **Integration Tests**: Create `test/test_pdf_to_view.c`
   - Test simple PDF with one text object
   - Test PDF with multiple text objects
   - Verify view tree structure

3. **Sample PDFs**: Create `test/input/simple_text.pdf`
   - Hand-crafted minimal PDFs for testing
   - One text object at known coordinates

### Success Criteria

- [ ] Parse basic PDF operators (BT, ET, Tj, Tf, Tm)
- [ ] Create ViewText nodes with correct positioning
- [ ] Pass unit tests for operator parsing
- [ ] Render simple text PDF to SVG successfully

## Phase 2: Graphics and Layout (Week 3-4)

### Goal
Add support for graphics operators, colors, and coordinate transformations.

### Deliverables

#### 2.1 Complete Graphics State Implementation

**Add to `pdf/operators.cpp`**:
- Color operators (rg, RG, g, G)
- Path operators (m, l, c, re)
- Graphics state save/restore (q, Q)
- Transformation matrix (cm)

#### 2.2 Shape Rendering

```cpp
// radiant/pdf/shapes.cpp
/**
 * Create ViewBlock for rectangles and paths
 */
ViewBlock* create_rect_view(Pool* pool, PDFOperator* op, PDFGraphicsState* state) {
    ViewBlock* rect = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
    rect->type = RDT_VIEW_BLOCK;
    rect->x = op->operands.rect.x;
    rect->y = op->operands.rect.y;
    rect->width = op->operands.rect.width;
    rect->height = op->operands.rect.height;

    // Apply fill color if specified
    if (state->fill_color[0] >= 0) {
        // Create background property
        BackgroundProp* bg = create_background(pool);
        bg->color.r = (uint8_t)(state->fill_color[0] * 255);
        bg->color.g = (uint8_t)(state->fill_color[1] * 255);
        bg->color.b = (uint8_t)(state->fill_color[2] * 255);
        bg->color.a = 255;

        // Attach to view
        rect->bound = create_boundary(pool);
        rect->bound->background = bg;
    }

    return rect;
}
```

#### 2.3 Font Mapping

```cpp
// radiant/pdf/fonts.cpp
/**
 * Map PDF font names to system fonts
 */
const char* map_pdf_font_to_system(const char* pdf_font) {
    // Standard 14 fonts mapping
    static const struct {
        const char* pdf_name;
        const char* system_name;
    } font_map[] = {
        {"Helvetica", "Arial"},
        {"Helvetica-Bold", "Arial-Bold"},
        {"Helvetica-Oblique", "Arial-Italic"},
        {"Times-Roman", "Times New Roman"},
        {"Times-Bold", "Times New Roman-Bold"},
        {"Times-Italic", "Times New Roman-Italic"},
        {"Courier", "Courier New"},
        {"Courier-Bold", "Courier New-Bold"},
        {"Symbol", "Symbol"},
        {"ZapfDingbats", "Zapf Dingbats"},
        {nullptr, nullptr}
    };

    for (int i = 0; font_map[i].pdf_name; i++) {
        if (strcmp(pdf_font, font_map[i].pdf_name) == 0) {
            return font_map[i].system_name;
        }
    }

    return "Arial"; // Default fallback
}

/**
 * Extract font descriptor information
 */
FontProp* create_font_from_pdf(Pool* pool, Map* font_dict) {
    FontProp* font = (FontProp*)pool_calloc(pool, sizeof(FontProp));

    // Get base font name
    String* base_font_key = create_string_from_literal(pool, "BaseFont");
    Item base_font_item = map_get(font_dict, base_font_key);

    if (base_font_item.item != ITEM_NULL) {
        String* base_font = (String*)base_font_item.item;
        font->family = map_pdf_font_to_system(base_font->chars);
    } else {
        font->family = "Arial";
    }

    // Extract font weight, style from name
    // e.g., "Helvetica-BoldOblique" -> Bold + Italic

    return font;
}
```

#### 2.4 Coordinate Transformation

```cpp
// radiant/pdf/coords.cpp
/**
 * Transform point from PDF coordinates to Radiant coordinates
 */
void pdf_to_radiant_coords(PDFGraphicsState* state, double page_height,
                           double* x, double* y) {
    // Apply text matrix transformation
    double tx = state->tm[0] * (*x) + state->tm[2] * (*y) + state->tm[4];
    double ty = state->tm[1] * (*x) + state->tm[3] * (*y) + state->tm[5];

    // Apply CTM (current transformation matrix)
    double ctx = state->ctm[0] * tx + state->ctm[2] * ty + state->ctm[4];
    double cty = state->ctm[1] * tx + state->ctm[3] * ty + state->ctm[5];

    // Convert from PDF coordinates (bottom-left origin) to Radiant (top-left origin)
    *x = ctx;
    *y = page_height - cty;
}

/**
 * Apply matrix transformation
 */
void apply_matrix_transform(double* matrix, double* x, double* y) {
    double tx = matrix[0] * (*x) + matrix[2] * (*y) + matrix[4];
    double ty = matrix[1] * (*x) + matrix[3] * (*y) + matrix[5];
    *x = tx;
    *y = ty;
}
```

#### 2.5 Radiant Screen Rendering Integration

**Goal**: Integrate PDF rendering into Radiant's interactive viewer, enabling real-time PDF display in the GUI.

**Add PDF document loading to `radiant/window.cpp`**:

```cpp
// radiant/window.cpp - Add PDF support to document loading

/**
 * Detect document type from file extension
 */
DocumentType detect_document_type(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return DOC_TYPE_HTML;

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return DOC_TYPE_HTML;
    } else if (strcmp(ext, ".pdf") == 0) {
        return DOC_TYPE_PDF;
    } else if (strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0) {
        return DOC_TYPE_MARKDOWN;
    }

    return DOC_TYPE_HTML; // default
}

/**
 * Load document based on type
 */
Document* load_document(lxb_url_t* base_url, const char* filename) {
    DocumentType doc_type = detect_document_type(filename);

    switch (doc_type) {
        case DOC_TYPE_HTML:
            return load_html_doc(base_url, (char*)filename);

        case DOC_TYPE_PDF:
            return load_pdf_doc(base_url, filename);

        case DOC_TYPE_MARKDOWN:
            // Future: Markdown -> HTML -> Document
            return load_html_doc(base_url, (char*)filename);

        default:
            return load_html_doc(base_url, (char*)filename);
    }
}
```

**Create PDF document loader in `radiant/pdf/loader.cpp`**:

```cpp
// radiant/pdf/loader.cpp
#include "../view.hpp"
#include "../dom.hpp"
#include "pdf_to_view.hpp"
#include "../../lambda/input/input.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Load PDF file and convert to Document structure for Radiant
 */
Document* load_pdf_doc(lxb_url_t* base_url, const char* filename) {
    log_info("Loading PDF document: %s", filename);

    // Read PDF file
    FILE* file = fopen(filename, "rb");
    if (!file) {
        log_error("Failed to open PDF file: %s", filename);
        return nullptr;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read content
    char* pdf_content = (char*)malloc(file_size + 1);
    if (!pdf_content) {
        log_error("Failed to allocate memory for PDF content");
        fclose(file);
        return nullptr;
    }

    size_t bytes_read = fread(pdf_content, 1, file_size, file);
    pdf_content[bytes_read] = '\0';
    fclose(file);

    // Create Input context
    Input* input = (Input*)calloc(1, sizeof(Input));
    input->pool = pool_create(1024 * 1024); // 1MB pool
    input->sb = stringbuf_new(input->pool);

    // Parse PDF
    log_debug("Parsing PDF content...");
    parse_pdf(input, pdf_content);

    if (input->root.item == ITEM_ERROR) {
        log_error("Failed to parse PDF");
        free(pdf_content);
        pool_destroy(input->pool);
        free(input);
        return nullptr;
    }

    // Convert to view tree
    log_debug("Converting PDF to view tree...");
    ViewTree* view_tree = pdf_to_view_tree(input, input->root);

    if (!view_tree || !view_tree->root) {
        log_error("Failed to convert PDF to view tree");
        free(pdf_content);
        pool_destroy(input->pool);
        free(input);
        return nullptr;
    }

    // Create Document structure
    Document* doc = (Document*)pool_calloc(view_tree->pool, sizeof(Document));
    doc->url = base_url;
    doc->view_tree = view_tree;
    doc->doc_type = DOC_TYPE_PDF;
    doc->pool = view_tree->pool;

    // Store input context for cleanup
    doc->pdf_input = input;

    // Store original PDF content for potential re-parsing
    doc->pdf_content = pdf_content;
    doc->pdf_content_length = bytes_read;

    log_info("PDF document loaded successfully");
    return doc;
}

/**
 * Cleanup PDF-specific resources
 */
void cleanup_pdf_doc(Document* doc) {
    if (!doc) return;

    if (doc->pdf_content) {
        free(doc->pdf_content);
        doc->pdf_content = nullptr;
    }

    if (doc->pdf_input) {
        Input* input = (Input*)doc->pdf_input;
        if (input->pool) {
            pool_destroy(input->pool);
        }
        free(input);
        doc->pdf_input = nullptr;
    }
}
```

**Update Document structure in `radiant/dom.hpp`**:

```cpp
// radiant/dom.hpp - Add PDF support to Document structure

typedef enum {
    DOC_TYPE_HTML,
    DOC_TYPE_PDF,
    DOC_TYPE_MARKDOWN,
    DOC_TYPE_XML
} DocumentType;

typedef struct Document {
    lxb_url_t* url;
    ViewTree* view_tree;
    DocumentType doc_type;          // NEW: Document type
    Pool* pool;

    // HTML-specific fields
    lxb_html_document_t* lxb_doc;
    DomNode* root_dom_node;
    Element* lambda_html_root;
    DomElement* lambda_dom_root;

    // PDF-specific fields (NEW)
    void* pdf_input;                // Input context for PDF
    char* pdf_content;              // Original PDF content
    size_t pdf_content_length;      // Content length

    // Common fields
    int page_count;                 // Number of pages (for PDF)
    int current_page;               // Current page index
} Document;
```

**Update window event handling in `radiant/window.cpp`**:

```cpp
// radiant/window.cpp - Add PDF-specific event handling

/**
 * Handle keyboard events for PDF navigation
 */
void handle_pdf_keyboard(GLFWwindow* window, int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    UiContext* uicon = (UiContext*)glfwGetWindowUserPointer(window);
    Document* doc = uicon->document;

    if (!doc || doc->doc_type != DOC_TYPE_PDF) return;

    switch (key) {
        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_RIGHT:
            // Next page
            if (doc->current_page < doc->page_count - 1) {
                doc->current_page++;
                reload_pdf_page(uicon, doc, doc->current_page);
            }
            break;

        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_LEFT:
            // Previous page
            if (doc->current_page > 0) {
                doc->current_page--;
                reload_pdf_page(uicon, doc, doc->current_page);
            }
            break;

        case GLFW_KEY_HOME:
            // First page
            if (doc->current_page != 0) {
                doc->current_page = 0;
                reload_pdf_page(uicon, doc, 0);
            }
            break;

        case GLFW_KEY_END:
            // Last page
            if (doc->current_page != doc->page_count - 1) {
                doc->current_page = doc->page_count - 1;
                reload_pdf_page(uicon, doc, doc->page_count - 1);
            }
            break;
    }
}

/**
 * Reload a specific PDF page
 */
void reload_pdf_page(UiContext* uicon, Document* doc, int page_index) {
    log_info("Loading PDF page %d/%d", page_index + 1, doc->page_count);

    // Re-parse PDF and extract specific page
    Input* input = (Input*)doc->pdf_input;

    // Parse PDF content again (or use cached parsed data)
    parse_pdf(input, doc->pdf_content);

    // Convert specific page to view tree
    ViewTree* new_view_tree = pdf_page_to_view_tree(input, input->root, page_index);

    if (new_view_tree && new_view_tree->root) {
        // Free old view tree
        if (doc->view_tree) {
            free_view_tree(doc->view_tree);
        }

        doc->view_tree = new_view_tree;

        // Re-layout
        layout_html_doc(uicon, doc, false);

        // Update window title with page number
        char title[256];
        snprintf(title, sizeof(title), "Lambda - %s (Page %d/%d)",
                lxb_url_path_str(doc->url), page_index + 1, doc->page_count);
        glfwSetWindowTitle(uicon->window, title);

        // Mark for redraw
        uicon->needs_redraw = true;
    }
}

/**
 * Main keyboard callback - route to appropriate handler
 */
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    UiContext* uicon = (UiContext*)glfwGetWindowUserPointer(window);

    if (!uicon || !uicon->document) return;

    // Route to document-specific handler
    if (uicon->document->doc_type == DOC_TYPE_PDF) {
        handle_pdf_keyboard(window, key, action);
    } else {
        handle_html_keyboard(window, key, action);
    }
}
```

**Update main window loop in `radiant/window.cpp`**:

```cpp
// radiant/window.cpp - Update window title to show PDF info

void update_window_title(UiContext* uicon) {
    if (!uicon->document) return;

    char title[256];
    const char* filename = lxb_url_path_str(uicon->document->url);

    if (uicon->document->doc_type == DOC_TYPE_PDF) {
        snprintf(title, sizeof(title), "Lambda PDF Viewer - %s (Page %d/%d)",
                filename,
                uicon->document->current_page + 1,
                uicon->document->page_count);
    } else {
        snprintf(title, sizeof(title), "Lambda - %s", filename);
    }

    glfwSetWindowTitle(uicon->window, title);
}
```

**Add PDF loader header in `radiant/pdf/loader.hpp`**:

```cpp
// radiant/pdf/loader.hpp
#ifndef PDF_LOADER_HPP
#define PDF_LOADER_HPP

#include "../dom.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load PDF document and convert to Radiant Document structure
 */
Document* load_pdf_doc(lxb_url_t* base_url, const char* filename);

/**
 * Cleanup PDF-specific resources
 */
void cleanup_pdf_doc(Document* doc);

/**
 * Convert a specific PDF page to view tree
 */
ViewTree* pdf_page_to_view_tree(Input* input, Item pdf_root, int page_index);

#ifdef __cplusplus
}
#endif

#endif // PDF_LOADER_HPP
```

**Usage Example**:

```bash
# Launch Radiant with PDF file
./radiant document.pdf

# Navigate with keyboard
# Page Down / Right Arrow - Next page
# Page Up / Left Arrow - Previous page
# Home - First page
# End - Last page
```

**Implementation Notes**:
- PDF files are detected by `.pdf` extension
- Each page is rendered as a separate view tree
- Keyboard navigation switches between pages
- Window title shows current page number
- Reuses existing Radiant rendering pipeline
- Memory is managed through pool allocation

### Testing Strategy

1. **Graphics Tests**: PDFs with rectangles, colors
2. **Font Tests**: PDFs using different fonts
3. **Transform Tests**: Rotated, scaled, translated content
4. **Integration Tests**: (NEW)
   - Load PDF in Radiant window
   - Test page navigation (PgUp/PgDn)
   - Verify rendering matches SVG output
   - Test multi-page PDFs
   - Memory leak testing with repeated page loads

### Success Criteria

- [ ] Render rectangles with fill colors
- [ ] Map PDF fonts to system fonts correctly
- [ ] Handle coordinate transformations (rotation, scaling)
- [ ] Render multi-colored text and shapes
- [ ] **(NEW)** Load PDF files from command line
- [ ] **(NEW)** Display PDF pages in Radiant window
- [ ] **(NEW)** Navigate between pages with keyboard
- [ ] **(NEW)** Update window title with page info
- [ ] **(NEW)** Handle PDF-specific events correctly

## Phase 3: Advanced Features (Week 5-6)

### Goal
Handle compressed streams, images, and complex layouts.

### Deliverables

#### 3.1 Stream Decompression

```cpp
// radiant/pdf/decompress.cpp
/**
 * Decompress PDF streams (FlateDecode, LZWDecode, etc.)
 */
String* decompress_stream(Pool* pool, String* compressed_data, String* filter_name) {
    if (strcmp(filter_name->chars, "FlateDecode") == 0) {
        return flate_decompress(pool, compressed_data);
    } else if (strcmp(filter_name->chars, "LZWDecode") == 0) {
        return lzw_decompress(pool, compressed_data);
    } else if (strcmp(filter_name->chars, "DCTDecode") == 0) {
        // JPEG data - pass through
        return compressed_data;
    }

    log_warn("Unsupported filter: %s", filter_name->chars);
    return nullptr;
}
```

Use existing libraries:
- **zlib** for FlateDecode
- **libjpeg** for DCTDecode (JPEG images)

#### 3.2 Image Handling

```cpp
// radiant/pdf/images.cpp
/**
 * Process inline images (BI...EI operators)
 */
ViewBlock* create_image_view(Pool* pool, PDFOperator* op, PDFGraphicsState* state) {
    ViewBlock* img_view = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
    img_view->type = RDT_VIEW_BLOCK;

    // Create ImageSurface from image data
    ImageSurface* img = (ImageSurface*)pool_calloc(pool, sizeof(ImageSurface));
    img->format = IMAGE_FORMAT_JPEG; // or PNG, based on filter

    // Decode image data
    // ...

    // Create embed property
    EmbedProp* embed = (EmbedProp*)pool_calloc(pool, sizeof(EmbedProp));
    embed->img = img;

    img_view->embed = embed;

    return img_view;
}
```

#### 3.3 Multi-Page Support

```cpp
// radiant/pdf/pages.cpp
/**
 * Process PDF page tree
 */
ViewTree* process_pdf_pages(Input* input, Map* pdf_data) {
    // Get trailer
    String* trailer_key = create_string_from_literal(input->pool, "trailer");
    Item trailer_item = map_get(pdf_data, trailer_key);

    if (trailer_item.item == ITEM_NULL) {
        log_warn("No trailer found");
        return nullptr;
    }

    Map* trailer = (Map*)trailer_item.item;

    // Get Root (Catalog)
    String* root_key = create_string_from_literal(input->pool, "Root");
    Item root_item = map_get(trailer, root_key);

    // Navigate to Pages tree
    // For each page:
    //   - Create ViewBlock for page
    //   - Process page's content streams
    //   - Add to document

    return view_tree;
}
```

#### 3.4 Text Extraction for Selection Layer

```cpp
// radiant/pdf/text_layer.cpp
/**
 * Generate HTML text selection layer (like PDF.js)
 * Optional feature for interactive viewing
 */
const char* generate_text_selection_html(ViewTree* tree) {
    StrBuf* html = strbuf_new_cap(4096);

    strbuf_append_str(html, "<div class=\"textLayer\" style=\"position: absolute;\">\n");

    // Walk view tree and output transparent text divs
    walk_text_views(tree->root, [&](ViewText* text_view) {
        strbuf_append_format(html,
            "  <span style=\"position: absolute; left: %.2fpx; top: %.2fpx; "
            "font-size: %.2fpx; color: transparent;\">%s</span>\n",
            text_view->x, text_view->y, text_view->font_size, text_view->text);
    });

    strbuf_append_str(html, "</div>\n");

    return html->str;
}
```

### Testing Strategy

1. **Compression Tests**: Real PDFs with compressed streams
2. **Image Tests**: PDFs with embedded images
3. **Multi-page Tests**: Multi-page PDF documents
4. **Selection Tests**: Verify text selection layer generation

### Success Criteria

- [ ] Decompress FlateDecode streams
- [ ] Render embedded JPEG images
- [ ] Handle multi-page PDFs
- [ ] Generate text selection layer HTML

## Phase 4: Integration and Optimization (Week 7-8)

### Goal
Integrate with existing Radiant pipeline and optimize performance.

### Deliverables

#### 4.1 Command-Line Interface

```cpp
// radiant/pdf/cmd_pdf.cpp
/**
 * Command to render PDF to various formats
 */
int cmd_pdf_render(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: lambda pdf-render <input.pdf> <output.svg|output.pdf>\n");
        return 1;
    }

    const char* pdf_file = argv[1];
    const char* output_file = argv[2];

    // Load PDF
    log_info("Loading PDF: %s", pdf_file);
    Input* input = create_input();
    input->pool = pool_create(1024 * 1024); // 1MB pool

    // Parse PDF
    char* pdf_content = read_file_to_string(pdf_file);
    parse_pdf(input, pdf_content);

    if (input->root.item == ITEM_ERROR) {
        log_error("Failed to parse PDF");
        return 1;
    }

    // Convert to view tree
    ViewTree* view_tree = pdf_to_view_tree(input, input->root);

    if (!view_tree) {
        log_error("Failed to convert PDF to view tree");
        return 1;
    }

    // Initialize UI context for rendering
    UiContext ui_context;
    ui_context_init(&ui_context, true); // headless mode

    // Render based on output format
    const char* ext = strrchr(output_file, '.');
    if (ext && strcmp(ext, ".svg") == 0) {
        // Render to SVG
        char* svg_content = render_view_tree_to_svg(&ui_context, view_tree->root,
                                                    612, 792);
        write_string_to_file(output_file, svg_content);
        log_info("Rendered to SVG: %s", output_file);
    } else if (ext && strcmp(ext, ".pdf") == 0) {
        // Render to PDF (re-encode)
        HPDF_Doc pdf_doc = render_view_tree_to_pdf(&ui_context, view_tree->root,
                                                   612, 792);
        save_pdf_to_file(pdf_doc, output_file);
        log_info("Rendered to PDF: %s", output_file);
    } else {
        log_error("Unsupported output format: %s", output_file);
        return 1;
    }

    // Cleanup
    ui_context_cleanup(&ui_context);
    pool_destroy(input->pool);
    free(pdf_content);

    return 0;
}
```

#### 4.2 Performance Optimization

**Caching Strategy**:
```cpp
// Cache parsed operators
typedef struct {
    Map* operator_cache;  // stream_data -> parsed operators
    Map* font_cache;      // font_name -> FontProp
    Map* image_cache;     // image_data -> ImageSurface
} PDFRenderCache;
```

**Memory Pooling**:
- Use separate pools for temporary parsing vs. final view tree
- Release parsing pool after conversion complete

**Lazy Loading**:
- Parse pages on-demand for multi-page documents
- Defer image decoding until rendering

#### 4.3 Error Handling

```cpp
// Robust error handling at each stage
typedef enum {
    PDF_ERROR_NONE = 0,
    PDF_ERROR_INVALID_FORMAT,
    PDF_ERROR_UNSUPPORTED_VERSION,
    PDF_ERROR_COMPRESSED_STREAM,
    PDF_ERROR_MISSING_FONT,
    PDF_ERROR_CORRUPT_STREAM,
    PDF_ERROR_OUT_OF_MEMORY
} PDFErrorCode;

typedef struct {
    PDFErrorCode code;
    const char* message;
    int line_number;
} PDFError;
```

#### 4.4 Documentation

Create comprehensive documentation:
- **API Reference**: `docs/PDF_Rendering_API.md`
- **User Guide**: `docs/PDF_Rendering_Guide.md`
- **Examples**: `examples/pdf_rendering/`

### Testing Strategy

1. **End-to-End Tests**: Real-world PDFs
2. **Performance Tests**: Large PDFs, memory usage
3. **Regression Tests**: Test suite with 100+ PDFs
4. **Compatibility Tests**: Various PDF versions

### Success Criteria

- [ ] Render complex real-world PDFs correctly
- [ ] Performance: < 100ms for simple PDFs
- [ ] Memory usage: < 10MB for typical PDFs
- [ ] Pass all regression tests

## Testing Framework

### Test Infrastructure

```bash
# Create test structure
test/
├── pdf/
│   ├── simple_text.pdf
│   ├── colored_shapes.pdf
│   ├── embedded_images.pdf
│   ├── multipage.pdf
│   └── compressed_streams.pdf
├── test_pdf_operators.c
├── test_pdf_to_view.c
├── test_pdf_rendering.c
└── expected/
    ├── simple_text.svg
    ├── colored_shapes.svg
    └── ...
```

### Automated Testing

```cpp
// test/test_pdf_rendering.c
Test(pdf_rendering, simple_text) {
    Input* input = create_input();
    input->pool = pool_create(1024 * 1024);

    // Load test PDF
    char* pdf_content = read_file_to_string("test/pdf/simple_text.pdf");
    parse_pdf(input, pdf_content);

    cr_assert_neq(input->root.item, ITEM_ERROR, "PDF should parse successfully");

    // Convert to view tree
    ViewTree* tree = pdf_to_view_tree(input, input->root);
    cr_assert_not_null(tree, "Should create view tree");
    cr_assert_not_null(tree->root, "Should have root view");

    // Verify structure
    ViewBlock* root = (ViewBlock*)tree->root;
    cr_assert_eq(root->type, RDT_VIEW_BLOCK, "Root should be ViewBlock");

    // Check for text views
    int text_count = count_text_views(tree->root);
    cr_assert_gt(text_count, 0, "Should have text views");

    // Cleanup
    pool_destroy(input->pool);
    free(pdf_content);
}
```

## File Structure

```
radiant/
├── pdf/
│   ├── pdf_to_view.hpp         # Main API header
│   ├── pdf_to_view.cpp         # Main conversion logic
│   ├── operators.h             # Operator definitions
│   ├── operators.cpp           # Operator parsing
│   ├── coords.cpp              # Coordinate transformations
│   ├── fonts.cpp               # Font mapping
│   ├── shapes.cpp              # Shape rendering
│   ├── decompress.cpp          # Stream decompression
│   ├── images.cpp              # Image handling
│   ├── pages.cpp               # Multi-page support
│   ├── text_layer.cpp          # Text selection layer
│   ├── loader.hpp              # NEW: Document loader API
│   ├── loader.cpp              # NEW: PDF document loading
│   └── cmd_pdf.cpp             # CLI commands
├── window.cpp                  # MODIFIED: PDF support in GUI
├── dom.hpp                     # MODIFIED: Document structure with PDF fields

lambda/input/
└── input-pdf.cpp               # Existing PDF parser (enhanced)

test/
├── test_pdf_operators.c
├── test_pdf_to_view.c
├── test_pdf_rendering.c
├── test_pdf_interactive.c      # NEW: GUI integration tests
└── pdf/                        # Test PDFs
```

## Dependencies

### Required Libraries
- **zlib**: FlateDecode decompression
- **libjpeg** or **stb_image**: Image decoding
- **Radiant**: Existing view tree and rendering (already present)

### Optional Libraries
- **libharu**: PDF output (already used in `render_pdf.cpp`)
- **FreeType**: Font rendering (already used in Radiant)

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Simple PDF (1 page, text only) | < 50ms | Parse + convert + render |
| Complex PDF (10 pages, images) | < 500ms | Full document |
| Memory usage (per page) | < 5MB | Excluding image data |
| Memory usage (images) | < 20MB | For typical documents |

## Milestones and Timeline

| Week | Milestone | Deliverable |
|------|-----------|-------------|
| 1-2 | Phase 1 Complete | Basic text rendering |
| 3-4 | Phase 2 Complete | Graphics and colors |
| 5-6 | Phase 3 Complete | Images and multi-page |
| 7-8 | Phase 4 Complete | Full integration |

## Future Enhancements

### Phase 5 (Optional)
- **Interactive Features**: Links, form fields, annotations
- **Advanced Text**: Unicode, right-to-left, vertical text
- **Advanced Graphics**: Gradients, patterns, transparency
- **PDF/A Support**: Archival format compliance
- **Incremental Rendering**: Render visible pages first
- **Thumbnail Generation**: Quick previews
- **Text Search**: Full-text search capabilities
- **Bookmarks/Outline**: Navigation tree

## References

### PDF.js Architecture
- Canvas rendering: `src/display/canvas.js`
- Text layer: `src/display/text_layer.js`
- Operator parsing: `src/core/evaluator.js`
- Font handling: `src/core/fonts.js`

### PDF Specification
- PDF Reference 1.7 (ISO 32000-1:2008)
- Text operators: Section 9.4
- Graphics operators: Section 8.5
- Content streams: Section 7.8

### Radiant Integration
- View tree: `radiant/view.hpp`
- Layout engine: `radiant/layout*.cpp`
- Rendering: `radiant/render*.cpp`

## Conclusion

This plan provides a structured, incremental approach to implementing PDF rendering in Lambda's Radiant engine. By following PDF.js's proven architecture while adapting to Radiant's view tree model, we can achieve high-fidelity PDF rendering with good performance.

The key innovation is **direct conversion** from PDF to Radiant's view tree, avoiding the complexity of intermediate HTML/CSS generation while maintaining the ability to leverage Radiant's powerful layout and rendering capabilities.
