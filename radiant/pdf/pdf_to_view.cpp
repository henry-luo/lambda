// radiant/pdf/pdf_to_view.cpp
// Main conversion logic from PDF to Radiant View Tree

#include "pdf_to_view.hpp"
#include "operators.h"
#include "../../lib/log.h"
#include "../../lambda/input/input.h"
#include "../../lambda/input/css/dom_element.h"
#include <string.h>

// Forward declarations
static ViewBlock* create_document_view(Pool* pool);
static void process_pdf_object(Input* input, ViewBlock* parent, Item obj_item);
static void process_pdf_stream(Input* input, ViewBlock* parent, Map* stream_map);
static void process_pdf_operator(Input* input, ViewBlock* parent, PDFStreamParser* parser, PDFOperator* op);
static void create_text_view(Input* input, ViewBlock* parent, PDFStreamParser* parser, String* text);
static void create_text_array_views(Input* input, ViewBlock* parent, PDFStreamParser* parser, Array* text_array);
static void create_rect_view(Input* input, ViewBlock* parent, PDFStreamParser* parser);
static void update_text_position(PDFStreamParser* parser, double tx, double ty);
static void append_child_view(View* parent, View* child);

// External declarations from fonts.cpp
extern FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size);

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
    view_tree->html_version = HTML5; // Treat as HTML5 for layout purposes

    // Extract PDF version and statistics
    String* version_key = input_create_string(input, "version");
    Item version_item = {.item = map_get(pdf_data, {.item = s2it(version_key)}).item};
    if (version_item.item != ITEM_NULL) {
        String* version = (String*)version_item.item;
        log_info("PDF version: %s", version->chars);
    }

    // Get objects array
    String* objects_key = input_create_string(input, "objects");
    Item objects_item = {.item = map_get(pdf_data, {.item = s2it(objects_key)}).item};

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
        log_debug("Processing object %d/%d", i+1, objects->length);
        process_pdf_object(input, root_view, obj_item);
    }

    log_info("PDF to View Tree conversion complete");

    // Count children to verify
    int child_count = 0;
    ViewGroup* group = (ViewGroup*)root_view;
    View* child = group->child;
    while (child) {
        child_count++;
        child = child->next;
    }
    log_info("Root view has %d children", child_count);

    return view_tree;
}

/**
 * Convert a specific PDF page to view tree
 */
ViewTree* pdf_page_to_view_tree(Input* input, Item pdf_root, int page_index) {
    log_info("Converting PDF page %d to view tree", page_index);

    // For Phase 1, just return the full document
    // TODO: Implement page selection in Phase 3
    return pdf_to_view_tree(input, pdf_root);
}

/**
 * Get number of pages in PDF
 */
int pdf_get_page_count(Item pdf_root) {
    // For Phase 1, return 1
    // TODO: Implement page counting in Phase 3
    return 1;
}

/**
 * Create root document view
 */
static ViewBlock* create_document_view(Pool* pool) {
    ViewBlock* root = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
    if (!root) {
        log_error("Failed to allocate root view");
        return nullptr;
    }

    root->type = RDT_VIEW_BLOCK;
    root->x = 0;
    root->y = 0;
    root->width = 612;  // Default letter width in points (8.5 inches * 72 dpi)
    root->height = 792; // Default letter height in points (11 inches * 72 dpi)

    log_debug("Created document view: %.0fx%.0f", root->width, root->height);

    return root;
}

/**
 * Process a single PDF object
 */
static void process_pdf_object(Input* input, ViewBlock* parent, Item obj_item) {
    if (obj_item.item == ITEM_NULL) {
        log_debug("Skipping null object");
        return;
    }

    // For maps, type_id is 0 (raw pointer), so check if item is not null
    // Maps and other complex types have type_id = 0
    TypeId actual_type = get_type_id(obj_item);

    if (actual_type == LMD_TYPE_NULL) {
        log_debug("Skipping null type object");
        return;
    }

    // Only process maps (type_id 0 means raw pointer to Map or other complex type)
    if (obj_item.type_id != 0 && actual_type != LMD_TYPE_MAP) {
        log_debug("Skipping non-map object (type_id=%d, actual_type=%d)", obj_item.type_id, actual_type);
        return;
    }

    Map* obj_map = (Map*)obj_item.item;

    // Check for type field
    String* type_key = input_create_string(input, "type");
    Item type_item = {.item = map_get(obj_map, {.item = s2it(type_key)}).item};

    if (type_item.item == ITEM_NULL) {
        log_debug("Object has no type field");
        return;
    }

    String* type_str = (String*)type_item.item;
    log_debug("Processing object of type: %s", type_str->chars);

    // Process stream objects
    if (strcmp(type_str->chars, "stream") == 0) {
        process_pdf_stream(input, parent, obj_map);
    }
    // Process indirect objects
    else if (strcmp(type_str->chars, "indirect_object") == 0) {
        String* content_key = input_create_string(input, "content");
        Item content_item = {.item = map_get(obj_map, {.item = s2it(content_key)}).item};
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
    String* data_key = input_create_string(input, "data");
    Item data_item = {.item = map_get(stream_map, {.item = s2it(data_key)}).item};

    if (data_item.item == ITEM_NULL) {
        log_warn("Stream has no data");
        return;
    }

    String* stream_data = (String*)data_item.item;

    // Get stream dictionary (contains Length, Filter, etc.)
    String* dict_key = input_create_string(input, "dictionary");
    Item dict_item = {.item = map_get(stream_map, {.item = s2it(dict_key)}).item};
    Map* stream_dict = (dict_item.item != ITEM_NULL) ? (Map*)dict_item.item : nullptr;

    // Check if stream is compressed
    if (stream_dict) {
        String* filter_key = input_create_string(input, "Filter");
        Item filter_item = {.item = map_get(stream_dict, {.item = s2it(filter_key)}).item};
        if (filter_item.item != ITEM_NULL) {
            log_warn("Compressed streams not yet supported (Phase 3)");
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
            // Begin text - reset text matrix to identity
            log_debug("Begin text");
            // Reset text matrix to identity [1 0 0 1 0 0]
            parser->state.tm[0] = 1.0;
            parser->state.tm[1] = 0.0;
            parser->state.tm[2] = 0.0;
            parser->state.tm[3] = 1.0;
            parser->state.tm[4] = 0.0;  // x position
            parser->state.tm[5] = 0.0;  // y position
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
            log_debug("Set text matrix: %.2f %.2f %.2f %.2f %.2f %.2f",
                     op->operands.text_matrix.a, op->operands.text_matrix.b,
                     op->operands.text_matrix.c, op->operands.text_matrix.d,
                     op->operands.text_matrix.e, op->operands.text_matrix.f);
            memcpy(parser->state.tm, &op->operands.text_matrix, sizeof(double) * 6);
            memcpy(parser->state.tlm, &op->operands.text_matrix, sizeof(double) * 6);
            break;

        case PDF_OP_Td:
            // Move text position - parser already updated tm, no need to call pdf_update_text_position
            log_debug("Move text position: %.2f, %.2f (tm already updated)",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            // The parser has already applied the Td operation to parser->state.tm[4] and tm[5]
            // So we don't need to call pdf_update_text_position() - that would double the values
            break;

        case PDF_OP_Tj:
            // Show text - create ViewText
            log_debug("Show text: %s", op->operands.show_text.text->chars);
            create_text_view(input, parent, parser, op->operands.show_text.text);
            break;

        case PDF_OP_TJ:
            // Show text array (with kerning adjustments)
            log_debug("Show text array");
            if (op->operands.text_array.array) {
                create_text_array_views(input, parent, parser, op->operands.text_array.array);
            }
            break;

        case PDF_OP_q:
            // Save graphics state
            log_debug("Save graphics state");
            pdf_graphics_state_save(&parser->state);
            break;

        case PDF_OP_Q:
            // Restore graphics state
            log_debug("Restore graphics state");
            pdf_graphics_state_restore(&parser->state);
            break;

        case PDF_OP_rg:
            // Set fill color (RGB)
            log_debug("Set fill color: %.2f %.2f %.2f",
                     op->operands.rgb_color.r,
                     op->operands.rgb_color.g,
                     op->operands.rgb_color.b);
            break;

        case PDF_OP_RG:
            // Set stroke color (RGB)
            log_debug("Set stroke color: %.2f %.2f %.2f",
                     op->operands.rgb_color.r,
                     op->operands.rgb_color.g,
                     op->operands.rgb_color.b);
            break;

        // Path construction operators
        case PDF_OP_m:
            // Move to
            log_debug("Move to: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            break;

        case PDF_OP_l:
            // Line to
            log_debug("Line to: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            break;

        case PDF_OP_c:
            // Cubic Bezier curve
            log_debug("Curve to: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f",
                     op->operands.text_matrix.a, op->operands.text_matrix.b,
                     op->operands.text_matrix.c, op->operands.text_matrix.d,
                     op->operands.text_matrix.e, op->operands.text_matrix.f);
            break;

        case PDF_OP_re:
            // Rectangle
            log_debug("Rectangle: %.2f, %.2f, %.2f x %.2f",
                     op->operands.rect.x, op->operands.rect.y,
                     op->operands.rect.width, op->operands.rect.height);
            // Store for rendering when path is painted
            break;

        case PDF_OP_h:
            // Close path
            log_debug("Close path");
            break;

        // Path painting operators
        case PDF_OP_S:
            // Stroke path
            log_debug("Stroke path");
            break;

        case PDF_OP_s:
            // Close and stroke path
            log_debug("Close and stroke path");
            break;

        case PDF_OP_f:
        case PDF_OP_F:
            // Fill path
            log_debug("Fill path");
            create_rect_view(input, parent, parser);
            break;

        case PDF_OP_f_star:
            // Fill path (even-odd)
            log_debug("Fill path (even-odd)");
            create_rect_view(input, parent, parser);
            break;

        case PDF_OP_B:
        case PDF_OP_B_star:
            // Fill and stroke
            log_debug("Fill and stroke path");
            create_rect_view(input, parent, parser);
            break;

        case PDF_OP_b:
        case PDF_OP_b_star:
            // Close, fill and stroke
            log_debug("Close, fill and stroke path");
            create_rect_view(input, parent, parser);
            break;

        case PDF_OP_n:
            // End path without painting
            log_debug("End path (no paint)");
            break;

        default:
            if (op->type != PDF_OP_UNKNOWN) {
                log_debug("Unhandled operator type: %d (%s)", op->type, op->name);
            }
            break;
    }
}

/**
 * Create a ViewBlock node for a rectangle/shape
 * This is called after path painting operators (f, F, S, B, etc.)
 */
static void create_rect_view(Input* input, ViewBlock* parent,
                             PDFStreamParser* parser) {
    // For now, we only handle the last rectangle operator
    // In a full implementation, we'd track path construction

    // Get the rectangle from current position (simplified)
    // In full implementation, we'd track the path state
    double x = parser->state.current_x;
    double y = parser->state.current_y;

    // Create ViewBlock for the rectangle
    ViewBlock* rect_view = (ViewBlock*)pool_calloc(input->pool, sizeof(ViewBlock));
    if (!rect_view) {
        log_error("Failed to allocate rect view");
        return;
    }

    rect_view->type = RDT_VIEW_BLOCK;
    rect_view->x = (float)x;
    rect_view->y = (float)y;  // Store PDF Y as-is
    rect_view->width = 100;   // TODO: Track from 're' operator
    rect_view->height = 100;  // TODO: Track from 're' operator

    // Create empty DomElement for styling
    DomElement* dom_elem = (DomElement*)pool_calloc(input->pool, sizeof(DomElement));
    if (dom_elem) {
        dom_elem->node_type = DOM_NODE_ELEMENT;
        dom_elem->tag_name = "div";  // Treat as div for layout
        dom_elem->parent = nullptr;
        dom_elem->next_sibling = nullptr;
        dom_elem->prev_sibling = nullptr;
        dom_elem->first_child = nullptr;
        dom_elem->pool = input->pool;
    }

    // Create DomNode wrapper
    DomNode* elem_node = (DomNode*)pool_calloc(input->pool, sizeof(DomNode));
    if (elem_node) {
        elem_node->type = LEXBOR_ELEMENT;
        elem_node->dom_element = dom_elem;
        elem_node->style = nullptr;
        elem_node->parent = nullptr;
    }

    rect_view->node = elem_node;

    // Apply fill color and/or stroke color from graphics state
    BoundaryProp* bound = nullptr;

    // Apply fill color if set
    if (parser->state.fill_color[0] >= 0.0) {
        if (!bound) {
            bound = (BoundaryProp*)pool_calloc(input->pool, sizeof(BoundaryProp));
        }

        if (bound) {
            // Create background property
            BackgroundProp* bg = (BackgroundProp*)pool_calloc(input->pool, sizeof(BackgroundProp));
            if (bg) {
                // Convert PDF RGB (0.0-1.0) to Color (0-255)
                bg->color.r = (uint8_t)(parser->state.fill_color[0] * 255.0);
                bg->color.g = (uint8_t)(parser->state.fill_color[1] * 255.0);
                bg->color.b = (uint8_t)(parser->state.fill_color[2] * 255.0);
                bg->color.a = 255; // Fully opaque
                bg->color.c = 1;   // Color is set

                bound->background = bg;

                log_debug("Applied fill color: RGB(%d, %d, %d)",
                         bg->color.r, bg->color.g, bg->color.b);
            }
        }
    }

    // Apply stroke color if set
    if (parser->state.stroke_color[0] >= 0.0) {
        if (!bound) {
            bound = (BoundaryProp*)pool_calloc(input->pool, sizeof(BoundaryProp));
        }

        if (bound) {
            // Create border property
            BorderProp* border = (BorderProp*)pool_calloc(input->pool, sizeof(BorderProp));
            if (border) {
                // Convert PDF RGB (0.0-1.0) to Color (0-255)
                Color stroke_color;
                stroke_color.r = (uint8_t)(parser->state.stroke_color[0] * 255.0);
                stroke_color.g = (uint8_t)(parser->state.stroke_color[1] * 255.0);
                stroke_color.b = (uint8_t)(parser->state.stroke_color[2] * 255.0);
                stroke_color.a = 255; // Fully opaque
                stroke_color.c = 1;   // Color is set

                // Apply stroke color to all four sides
                border->top_color = stroke_color;
                border->right_color = stroke_color;
                border->bottom_color = stroke_color;
                border->left_color = stroke_color;

                // Set border width (use line width from graphics state, default to 1.0)
                float line_width = parser->state.line_width > 0 ? parser->state.line_width : 1.0f;
                border->width.top = line_width;
                border->width.right = line_width;
                border->width.bottom = line_width;
                border->width.left = line_width;

                // Set border style to solid
                border->top_style = LXB_CSS_VALUE_SOLID;
                border->right_style = LXB_CSS_VALUE_SOLID;
                border->bottom_style = LXB_CSS_VALUE_SOLID;
                border->left_style = LXB_CSS_VALUE_SOLID;

                bound->border = border;

                log_debug("Applied stroke color: RGB(%d, %d, %d), width: %.2f",
                         stroke_color.r, stroke_color.g, stroke_color.b, line_width);
            }
        }
    }

    // Attach boundary if created
    if (bound) {
        rect_view->bound = bound;
    }

    // Add to parent
    append_child_view((View*)parent, (View*)rect_view);

    log_debug("Created rect view at (%.2f, %.2f)", x, y);
}

/**
 * Create a ViewText node from PDF text
 */
static void create_text_view(Input* input, ViewBlock* parent,
                            PDFStreamParser* parser, String* text) {
    if (!text || text->len == 0) return;

    // Calculate position from text matrix
    double x = parser->state.tm[4];  // e component (x translation)
    double y = parser->state.tm[5];  // f component (y translation)

    // Store PDF coordinates as-is (bottom-left origin)
    // Conversion to screen coordinates will be done during rendering

    // Create ViewText
    ViewText* text_view = (ViewText*)pool_calloc(input->pool, sizeof(ViewText));
    if (!text_view) {
        log_error("Failed to allocate text view");
        return;
    }

    text_view->type = RDT_VIEW_TEXT;
    text_view->x = (float)x;
    text_view->y = (float)y;  // Store PDF Y as-is
    text_view->width = 0;  // Will be calculated during layout
    text_view->height = (float)parser->state.font_size;

    log_debug("Created text view at (%.2f, %.2f): '%s'", x, y, text->chars);

    // Create DomText for the text content
    DomText* dom_text = (DomText*)pool_calloc(input->pool, sizeof(DomText));
    if (dom_text) {
        dom_text->node_type = DOM_NODE_TEXT;
        dom_text->text = text->chars;
        dom_text->length = text->len;
        dom_text->parent = nullptr;
        dom_text->next_sibling = nullptr;
        dom_text->prev_sibling = nullptr;
        dom_text->pool = input->pool;
    }

    // Create DomNode wrapper
    DomNode* text_node = (DomNode*)pool_calloc(input->pool, sizeof(DomNode));
    if (text_node) {
        text_node->type = MARK_TEXT;
        text_node->dom_text = dom_text;
        text_node->style = nullptr;
        text_node->parent = nullptr;
    }

    text_view->node = text_node;

    // Create font property using proper font descriptor parsing
    if (parser->state.font_name) {
        FontProp* font = create_font_from_pdf(input->pool,
                                              parser->state.font_name->chars,
                                              parser->state.font_size);
        if (font) {
            text_view->font = font;
        }
    }

    // Apply text color from graphics state (fill color)
    // Note: ViewText doesn't have InlineProp, so we store color in a custom way
    // For full color support, we'd wrap this in a ViewSpan
    // For now, log the color for Phase 2.4 completion tracking
    log_debug("Text fill color: RGB(%.2f, %.2f, %.2f)",
             parser->state.fill_color[0],
             parser->state.fill_color[1],
             parser->state.fill_color[2]);

    // Add to parent
    append_child_view((View*)parent, (View*)text_view);
}

/**
 * Create ViewText nodes from TJ operator text array
 * TJ array format: [(string) num (string) num ...] where num is horizontal displacement in 1/1000 em
 */
static void create_text_array_views(Input* input, ViewBlock* parent,
                                    PDFStreamParser* parser, Array* text_array) {
    if (!text_array || text_array->length == 0) return;

    // TJ array contains alternating strings and numbers
    // Strings are text to show, numbers are kerning adjustments (negative = move right)
    double x_offset = 0.0;  // Accumulated horizontal offset

    for (int i = 0; i < text_array->length; i++) {
        Item item = text_array->items[i];

        // Check if it's a string (text to show)
        if (item.type_id == LMD_TYPE_STRING) {
            String* text = (String*)item.item;
            if (text && text->len > 0) {
                // Temporarily adjust text matrix for this text segment
                double saved_x = parser->state.tm[4];
                parser->state.tm[4] += x_offset;

                create_text_view(input, parent, parser, text);

                // Restore x position
                parser->state.tm[4] = saved_x;

                // Advance by the width of the text (simplified - assumes 1 unit per character)
                x_offset += text->len * parser->state.font_size * 0.5;
            }
        }
        // Check if it's a number (kerning adjustment)
        else if (item.type_id == LMD_TYPE_INT || item.type_id == LMD_TYPE_FLOAT) {
            // Kerning is in 1/1000 of an em, negative values move to the right
            double kerning = 0.0;
            if (item.type_id == LMD_TYPE_INT) {
                kerning = -(double)item.int_val / 1000.0 * parser->state.font_size;
            } else {
                // For float, need to dereference pointer
                double* double_ptr = (double*)item.pointer;
                if (double_ptr) {
                    kerning = -*double_ptr / 1000.0 * parser->state.font_size;
                }
            }
            x_offset += kerning;
        }
    }

    log_debug("Processed TJ text array with %lld elements", text_array->length);
}

/**
 * Append a child view to a parent view
 */
static void append_child_view(View* parent, View* child) {
    if (!parent || !child) return;

    ViewGroup* parent_group = (ViewGroup*)parent;

    // Set parent reference
    child->parent = parent_group;

    // Append to child list
    if (!parent_group->child) {
        parent_group->child = child;
    } else {
        View* last = parent_group->child;
        while (last->next) {
            last = last->next;
        }
        last->next = child;
    }
}
