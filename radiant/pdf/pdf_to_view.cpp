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
static void update_text_position(PDFStreamParser* parser, double tx, double ty);
static void append_child_view(View* parent, View* child);

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
        process_pdf_object(input, root_view, obj_item);
    }

    log_info("PDF to View Tree conversion complete");
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
    if (obj_item.item == ITEM_NULL) return;

    // Check if this is a map (could be a stream or indirect object)
    if (obj_item.type_id != LMD_TYPE_MAP) return;

    Map* obj_map = (Map*)obj_item.item;

    // Check for type field
    String* type_key = input_create_string(input, "type");
    Item type_item = {.item = map_get(obj_map, {.item = s2it(type_key)}).item};

    if (type_item.item == ITEM_NULL) return;

    String* type_str = (String*)type_item.item;

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
            log_debug("Set text matrix: %.2f %.2f %.2f %.2f %.2f %.2f",
                     op->operands.text_matrix.a, op->operands.text_matrix.b,
                     op->operands.text_matrix.c, op->operands.text_matrix.d,
                     op->operands.text_matrix.e, op->operands.text_matrix.f);
            memcpy(parser->state.tm, &op->operands.text_matrix, sizeof(double) * 6);
            memcpy(parser->state.tlm, &op->operands.text_matrix, sizeof(double) * 6);
            break;

        case PDF_OP_Td:
            // Move text position
            log_debug("Move text position: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            pdf_update_text_position(&parser->state,
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
            log_debug("Show text array (Phase 2 feature)");
            // TODO: Implement in Phase 2
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

        default:
            if (op->type != PDF_OP_UNKNOWN) {
                log_debug("Unhandled operator type: %d (%s)", op->type, op->name);
            }
            break;
    }
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

    // Convert PDF coordinates (bottom-left origin) to Radiant coordinates (top-left origin)
    // PDF y increases upward, Radiant y increases downward
    double radiant_y = parent->height - y;

    // Create ViewText
    ViewText* text_view = (ViewText*)pool_calloc(input->pool, sizeof(ViewText));
    if (!text_view) {
        log_error("Failed to allocate text view");
        return;
    }

    text_view->type = RDT_VIEW_TEXT;
    text_view->x = (float)x;
    text_view->y = (float)radiant_y;
    text_view->width = 0;  // Will be calculated during layout
    text_view->height = (float)parser->state.font_size;

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

    // Create font property
    if (parser->state.font_name) {
        FontProp* font = (FontProp*)pool_calloc(input->pool, sizeof(FontProp));
        if (font) {
            // Map PDF font to system font (simplified for Phase 1)
            const char* family_name;
            if (strstr(parser->state.font_name->chars, "Helvetica")) {
                family_name = "Arial";
            } else if (strstr(parser->state.font_name->chars, "Times")) {
                family_name = "Times New Roman";
            } else if (strstr(parser->state.font_name->chars, "Courier")) {
                family_name = "Courier New";
            } else {
                family_name = "Arial"; // Default fallback
            }

            // Allocate and copy the font family name
            font->family = (char*)pool_alloc(input->pool, strlen(family_name) + 1);
            strcpy(font->family, family_name);

            font->font_size = (float)parser->state.font_size;
            font->font_style = LXB_CSS_VALUE_NORMAL;
            font->font_weight = LXB_CSS_VALUE_NORMAL;

            text_view->font = font;
        }
    }

    // Add to parent
    append_child_view((View*)parent, (View*)text_view);

    log_debug("Created text view at (%.2f, %.2f): '%s'", x, radiant_y, text->chars);
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
