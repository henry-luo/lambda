// radiant/pdf/pdf_to_view.cpp
// Main conversion logic from PDF to Radiant View Tree

#include "pdf_to_view.hpp"
#include "operators.h"
#include "pages.hpp"
#include "pdf_fonts.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lambda/input/input.hpp"
#include "../../lambda/mark_builder.hpp"
#include "../../lambda/input/css/dom_element.hpp"
#include "../render.hpp"  // For RenderContext and ThorVG access
#include <thorvg_capi.h>  // ThorVG C API
#include <string.h>
#include <math.h>         // For pow() in color space gamma correction

// PDF stream decompression
extern "C" {
    #include "../../lambda/input/pdf_decompress.h"
}

// Local helper function
static inline String* input_create_string(Input* input, const char* str) {
    MarkBuilder builder(input);
    return builder.createString(str);
}

// Forward declarations
static ViewBlock* create_document_view(Pool* pool);
static void process_pdf_object(Input* input, Pool* view_pool, ViewBlock* parent, Item obj_item);
static void process_pdf_stream(Input* input, Pool* view_pool, ViewBlock* parent, Map* stream_map, Map* resources, Map* pdf_data);
static void process_pdf_operator(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, PDFOperator* op);
static void create_text_view(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, String* text);
static void create_text_view_raw(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, String* text);
static void create_text_array_views(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, Array* text_array);
static void handle_do_operator(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, const char* xobject_name);
static void handle_image_xobject(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, Map* image_dict, const char* name);
static void handle_form_xobject(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, Map* form_dict, const char* name);
static ImageSurface* decode_raw_image_data(Map* image_dict, String* data, int width, int height, int bpc, Pool* pool);
static ImageSurface* decode_image_data(const uint8_t* data, size_t length, ImageFormat format_hint);
static void lookup_font_entry(PDFStreamParser* parser, const char* font_name);

// Color space handling forward declarations
static PDFColorSpaceInfo* parse_color_space(Input* input, Pool* pool, Item cs_item, Map* resources);
static void apply_color_space_to_rgb(PDFColorSpaceInfo* cs_info, const double* components, double* rgb_out);
static PDFColorSpaceInfo* lookup_named_colorspace(Input* input, Pool* pool, const char* cs_name, Map* resources);

// Path paint operation types
typedef enum {
    PAINT_FILL_ONLY,      // f, F, f* operators
    PAINT_STROKE_ONLY,    // S, s operators
    PAINT_FILL_AND_STROKE // B, B*, b, b* operators
} PaintOperation;

static void create_rect_view(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, PaintOperation paint_op);
static void create_path_view_thorvg(Input* input, Pool* view_pool, ViewBlock* parent, PDFStreamParser* parser, PaintOperation paint_op);
static bool needs_thorvg_path_render(PDFStreamParser* parser);
static void update_text_position(PDFStreamParser* parser, double tx, double ty);
static void append_child_view(View* parent, View* child);

// External declarations from fonts.cpp
extern FontProp* create_font_from_pdf(Pool* pool, const char* font_name, double font_size);

/**
 * Convert PDF data to Radiant View Tree
 *
 * This is the main entry point for PDF rendering.
 * Takes parsed PDF data and generates a view tree suitable for Radiant layout.
 * @param pixel_ratio Display scaling factor (e.g., 2.0 for Retina). Pass 1.0 if unknown.
 */
ViewTree* pdf_to_view_tree(Input* input, Item pdf_root, float pixel_ratio) {
    log_info("Starting PDF to View Tree conversion (pixel_ratio=%.2f)", pixel_ratio);

    if (pdf_root.item == ITEM_NULL || pdf_root.item == ITEM_ERROR) {
        log_error("Invalid PDF data");
        return nullptr;
    }

    Map* pdf_data = pdf_root.map;

    // Create a dedicated pool for the view tree (separate from input->pool)
    Pool* view_pool = pool_create();
    if (!view_pool) {
        log_error("Failed to create view pool for PDF");
        return nullptr;
    }

    // Create view tree
    ViewTree* view_tree = (ViewTree*)pool_calloc(view_pool, sizeof(ViewTree));
    if (!view_tree) {
        log_error("Failed to allocate view tree");
        pool_destroy(view_pool);
        return nullptr;
    }

    view_tree->pool = view_pool;
    view_tree->html_version = HTML5; // Treat as HTML5 for layout purposes

    // Extract PDF version and statistics
    String* version_key = input_create_string(input, "version");
    Item version_item = {.item = map_get(pdf_data, {.item = s2it(version_key)}).item};
    if (version_item.item != ITEM_NULL) {
        String* version = version_item.get_string();
        log_info("PDF version: %s", version->chars);
    }

    // Get objects array
    String* objects_key = input_create_string(input, "objects");
    Item objects_item = {.item = map_get(pdf_data, {.item = s2it(objects_key)}).item};

    if (objects_item.item == ITEM_NULL) {
        log_warn("No objects found in PDF");
        return view_tree;
    }

    Array* objects = objects_item.array;
    log_info("Processing %d PDF objects", objects->length);

    // Create root view (represents the document)
    ViewBlock* root_view = create_document_view(view_pool);
    view_tree->root = (View*)root_view;

    // Process each object looking for content streams
    for (int i = 0; i < objects->length; i++) {
        Item obj_item = objects->items[i];
        log_debug("Processing object %d/%d", i+1, objects->length);
        process_pdf_object(input, view_pool, root_view, obj_item);
    }

    log_info("PDF to View Tree conversion complete");

    // Count children to verify
    int child_count = 0;
    ViewElement* group = (ViewElement*)root_view;
    View* child = group->first_child;
    while (child) {
        child_count++;
        child = child->next_sibling;
    }
    log_info("Root view has %d children", child_count);

    // Warn if no content was extracted (likely due to compression)
    if (child_count == 0) {
        log_warn("No content extracted from PDF - this may be due to compressed streams (Phase 3 feature)");
    }

    // Scale view tree for high-DPI displays
    if (pixel_ratio > 1.0f) {
        pdf_scale_view_tree(view_tree, pixel_ratio);
    }

    return view_tree;
}

/**
 * Scale a view and all its children recursively by pixel_ratio
 */
static void scale_view_recursive(View* view, float pixel_ratio) {
    if (!view) return;

    // Scale position and dimensions
    view->x *= pixel_ratio;
    view->y *= pixel_ratio;
    view->width *= pixel_ratio;
    view->height *= pixel_ratio;

    // Scale VectorPathProp if present (for curves and dashed lines)
    if (view->view_type == RDT_VIEW_BLOCK) {
        ViewBlock* block = (ViewBlock*)view;
        if (block->vpath) {
            block->vpath->stroke_width *= pixel_ratio;
            for (VectorPathSegment* seg = block->vpath->segments; seg; seg = seg->next) {
                seg->x *= pixel_ratio;
                seg->y *= pixel_ratio;
                seg->x1 *= pixel_ratio;
                seg->y1 *= pixel_ratio;
                seg->x2 *= pixel_ratio;
                seg->y2 *= pixel_ratio;
            }
            // Scale dash pattern
            if (block->vpath->dash_pattern) {
                for (int i = 0; i < block->vpath->dash_pattern_length; i++) {
                    block->vpath->dash_pattern[i] *= pixel_ratio;
                }
            }
        }

        // Scale border widths if present
        if (block->bound && block->bound->border) {
            block->bound->border->width.top *= pixel_ratio;
            block->bound->border->width.right *= pixel_ratio;
            block->bound->border->width.bottom *= pixel_ratio;
            block->bound->border->width.left *= pixel_ratio;
        }
    }

    // Scale text font size and TextRect positions if present
    if (view->view_type == RDT_VIEW_TEXT) {
        ViewText* text = (ViewText*)view;
        if (text->font) {
            text->font->font_size *= pixel_ratio;
        }
        // Scale all TextRects (PDF text uses rect for position)
        for (TextRect* rect = text->rect; rect; rect = rect->next) {
            rect->x *= pixel_ratio;
            rect->y *= pixel_ratio;
            rect->width *= pixel_ratio;
            rect->height *= pixel_ratio;
        }
        // Text views have no children, return early
        return;
    }

    // Only block-type views have children - cast to ViewElement and recurse
    if (view->view_type == RDT_VIEW_BLOCK || view->view_type == RDT_VIEW_INLINE_BLOCK ||
        view->view_type == RDT_VIEW_LIST_ITEM || view->view_type == RDT_VIEW_TABLE ||
        view->view_type == RDT_VIEW_TABLE_ROW_GROUP || view->view_type == RDT_VIEW_TABLE_ROW ||
        view->view_type == RDT_VIEW_TABLE_CELL) {
        ViewElement* elem = (ViewElement*)view;
        View* child = (View*)elem->first_child;
        while (child) {
            scale_view_recursive(child, pixel_ratio);
            child = child->next_sibling;
        }
    }
}

/**
 * Scale PDF view tree by pixel ratio for high-DPI displays
 */
void pdf_scale_view_tree(ViewTree* view_tree, float pixel_ratio) {
    if (!view_tree || !view_tree->root || pixel_ratio <= 0) return;

    log_info("Scaling PDF view tree by pixel_ratio=%.2f", pixel_ratio);
    scale_view_recursive(view_tree->root, pixel_ratio);
}

/**
 * Convert a specific PDF page to view tree
 * @param pixel_ratio Display scaling factor (e.g., 2.0 for Retina). Pass 1.0 if unknown.
 */
ViewTree* pdf_page_to_view_tree(Input* input, Item pdf_root, int page_index, float pixel_ratio) {
    log_info("Converting PDF page %d to view tree (pixel_ratio=%.2f)", page_index + 1, pixel_ratio);

    if (pdf_root.item == ITEM_NULL || pdf_root.item == ITEM_ERROR) {
        log_error("Invalid PDF data");
        return nullptr;
    }

    Map* pdf_data = pdf_root.map;

    // Get page information
    PDFPageInfo* page_info = pdf_get_page_info(pdf_data, page_index, input->pool);
    if (!page_info) {
        log_error("Could not extract page info for page %d", page_index + 1);
        return nullptr;
    }

    // Create a dedicated pool for the view tree (separate from input->pool)
    // This pool will be destroyed when the view tree is freed via view_pool_destroy
    Pool* view_pool = pool_create();
    if (!view_pool) {
        log_error("Failed to create view pool for PDF");
        return nullptr;
    }

    // Create view tree using the dedicated view pool
    ViewTree* view_tree = (ViewTree*)pool_calloc(view_pool, sizeof(ViewTree));
    if (!view_tree) {
        log_error("Failed to allocate view tree");
        pool_destroy(view_pool);
        return nullptr;
    }

    view_tree->pool = view_pool;  // Use dedicated pool, not input->pool
    view_tree->html_version = HTML5;

    // Create root view with page dimensions
    ViewBlock* root_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!root_view) {
        log_error("Failed to allocate root view");
        pool_destroy(view_pool);
        return nullptr;
    }

    root_view->view_type = RDT_VIEW_BLOCK;
    root_view->x = page_info->media_box[0];
    root_view->y = page_info->media_box[1];
    root_view->width = page_info->media_box[2] - page_info->media_box[0];
    root_view->height = page_info->media_box[3] - page_info->media_box[1];

    log_debug("Created page view: %.0fx%.0f at (%.0f, %.0f)",
             root_view->width, root_view->height, root_view->x, root_view->y);

    view_tree->root = (View*)root_view;

    // Process all content streams for this page
    if (page_info->content_streams) {
        for (int i = 0; i < page_info->content_streams->length; i++) {
            Item stream_item = page_info->content_streams->items[i];
            if (stream_item.item == ITEM_NULL) continue;

            Map* stream_map = stream_item.map;
            log_debug("Processing content stream %d/%d for page %d",
                     i + 1, page_info->content_streams->length, page_index + 1);
            process_pdf_stream(input, view_pool, root_view, stream_map, page_info->resources, pdf_data);
        }
    }

    log_info("Page %d conversion complete", page_index + 1);

    // Count children to verify
    int child_count = 0;
    ViewElement* group = (ViewElement*)root_view;
    View* child = group->first_child;
    while (child) {
        child_count++;
        child = child->next_sibling;
    }
    log_info("Page %d has %d view elements", page_index + 1, child_count);

    // Scale view tree for high-DPI displays
    if (pixel_ratio > 1.0f) {
        pdf_scale_view_tree(view_tree, pixel_ratio);
    }

    return view_tree;
}

/**
 * Get number of pages in PDF
 */
int pdf_get_page_count(Item pdf_root) {
    if (pdf_root.item == ITEM_NULL || pdf_root.item == ITEM_ERROR) {
        log_error("Invalid PDF data");
        return 0;
    }

    Map* pdf_data = pdf_root.map;
    return pdf_get_page_count_from_data(pdf_data);
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

    root->view_type = RDT_VIEW_BLOCK;
    root->x = 0;
    root->y = 0;
    root->width = 612;  // Default letter width in points (8.5 inches * 72 dpi)
    root->height = 792; // Default letter height in points (11 inches * 72 dpi)

    log_debug("Created document view: %.0fx%.0f", root->width, root->height);

    return root;
}

// ============================================================================
// Color Space Handling
// ============================================================================

/**
 * Parse a color space definition from a PDF item
 * Color spaces can be:
 *   - Name: "DeviceRGB", "DeviceGray", "DeviceCMYK"
 *   - Array: [/Indexed /DeviceRGB 255 <lookup_data>]
 *            [/ICCBased <stream_ref>]
 *            [/CalGray {WhitePoint [...] Gamma 1.0}]
 *            [/CalRGB {WhitePoint [...] Gamma [...]}]
 */
static PDFColorSpaceInfo* parse_color_space(Input* input, Pool* pool, Item cs_item, Map* resources) {
    if (cs_item.item == ITEM_NULL) return nullptr;

    PDFColorSpaceInfo* info = (PDFColorSpaceInfo*)pool_calloc(pool, sizeof(PDFColorSpaceInfo));
    if (!info) return nullptr;

    // Default values
    info->gamma[0] = info->gamma[1] = info->gamma[2] = 1.0;
    info->white_point[0] = 0.9505; // D65 white point X
    info->white_point[1] = 1.0;    // D65 white point Y
    info->white_point[2] = 1.0890; // D65 white point Z

    TypeId type = get_type_id(cs_item);

    // Simple name color space (e.g., "DeviceRGB")
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_SYMBOL) {
        String* cs_name = cs_item.get_string();
        if (!cs_name) return nullptr;

        info->name = cs_name;

        if (strcmp(cs_name->chars, "DeviceRGB") == 0 || strcmp(cs_name->chars, "RGB") == 0) {
            info->type = PDF_CS_DEVICE_RGB;
            info->num_components = 3;
        } else if (strcmp(cs_name->chars, "DeviceGray") == 0 || strcmp(cs_name->chars, "G") == 0) {
            info->type = PDF_CS_DEVICE_GRAY;
            info->num_components = 1;
        } else if (strcmp(cs_name->chars, "DeviceCMYK") == 0 || strcmp(cs_name->chars, "CMYK") == 0) {
            info->type = PDF_CS_DEVICE_CMYK;
            info->num_components = 4;
        } else {
            // Might be a named color space reference - look up in resources
            PDFColorSpaceInfo* resolved = lookup_named_colorspace(input, pool, cs_name->chars, resources);
            if (resolved) {
                return resolved;
            }
            log_debug("Unknown color space name: %s, defaulting to DeviceRGB", cs_name->chars);
            info->type = PDF_CS_DEVICE_RGB;
            info->num_components = 3;
        }
        return info;
    }

    // Map type - this could be an ICCBased stream or similar
    if (type == LMD_TYPE_MAP) {
        Map* cs_map = cs_item.map;
        if (!cs_map) return nullptr;

        // Check if it's an ICCBased stream by looking for /N key
        String* n_key = input_create_string(input, "N");
        Item n_item = {.item = map_get(cs_map, {.item = s2it(n_key)}).item};

        if (n_item.item != ITEM_NULL) {
            // It's an ICCBased stream
            info->type = PDF_CS_ICCBASED;
            if (get_type_id(n_item) == LMD_TYPE_INT) {
                info->icc_n = n_item.int_val;
            } else if (get_type_id(n_item) == LMD_TYPE_FLOAT) {
                info->icc_n = (int)n_item.get_double();
            } else {
                info->icc_n = 3;  // Default to RGB
            }
            info->num_components = info->icc_n;
            log_debug("Map color space: ICCBased with N=%d", info->icc_n);
            return info;
        }

        // Unknown map type - default to DeviceGray (common for single-component spaces)
        log_debug("Unknown map color space, defaulting to DeviceGray");
        info->type = PDF_CS_DEVICE_GRAY;
        info->num_components = 1;
        return info;
    }

    // Array color space (e.g., [/Indexed /DeviceRGB 255 <data>])
    if (type == LMD_TYPE_ARRAY) {
        Array* cs_array = cs_item.array;
        if (!cs_array || cs_array->length < 1) return nullptr;

        // First element is the color space type name
        Item type_item = cs_array->items[0];
        String* type_name = type_item.get_string();
        if (!type_name) return nullptr;

        log_debug("Parsing array color space: %s", type_name->chars);

        if (strcmp(type_name->chars, "Indexed") == 0 || strcmp(type_name->chars, "I") == 0) {
            // [/Indexed base hival lookup]
            info->type = PDF_CS_INDEXED;
            info->num_components = 1;  // Indexed uses single index value

            if (cs_array->length >= 4) {
                // Base color space
                Item base_item = cs_array->items[1];
                PDFColorSpaceInfo* base_info = parse_color_space(input, pool, base_item, resources);
                if (base_info) {
                    info->base_type = base_info->type;
                } else {
                    info->base_type = PDF_CS_DEVICE_RGB;  // Default
                }

                // hival (max index)
                Item hival_item = cs_array->items[2];
                if (get_type_id(hival_item) == LMD_TYPE_INT) {
                    info->hival = hival_item.int_val;
                } else if (get_type_id(hival_item) == LMD_TYPE_FLOAT) {
                    info->hival = (int)hival_item.get_double();
                }

                // Lookup table
                Item lookup_item = cs_array->items[3];
                TypeId lookup_type = get_type_id(lookup_item);
                if (lookup_type == LMD_TYPE_STRING || lookup_type == LMD_TYPE_BINARY) {
                    String* lookup_data = lookup_item.get_string();
                    if (lookup_data) {
                        info->lookup_table = (uint8_t*)lookup_data->chars;
                        info->lookup_table_size = lookup_data->len;
                        log_debug("Indexed color space: hival=%d, lookup_size=%d, base=%d",
                                 info->hival, info->lookup_table_size, info->base_type);
                    }
                }
            }
            return info;
        }

        if (strcmp(type_name->chars, "ICCBased") == 0) {
            // [/ICCBased stream]
            info->type = PDF_CS_ICCBASED;

            // The stream contains ICC profile data and N (number of components)
            if (cs_array->length >= 2) {
                Item stream_item = cs_array->items[1];
                if (get_type_id(stream_item) == LMD_TYPE_MAP) {
                    Map* stream_dict = stream_item.map;

                    // Get N (number of components)
                    String* n_key = input_create_string(input, "N");
                    Item n_item = {.item = map_get(stream_dict, {.item = s2it(n_key)}).item};
                    if (n_item.item != ITEM_NULL) {
                        if (get_type_id(n_item) == LMD_TYPE_INT) {
                            info->icc_n = n_item.int_val;
                        } else if (get_type_id(n_item) == LMD_TYPE_FLOAT) {
                            info->icc_n = (int)n_item.get_double();
                        }
                    } else {
                        info->icc_n = 3;  // Default to 3 (RGB)
                    }
                    info->num_components = info->icc_n;
                    log_debug("ICCBased color space: N=%d", info->icc_n);
                }
            }
            return info;
        }

        if (strcmp(type_name->chars, "CalGray") == 0) {
            // [/CalGray <<dict>>]
            info->type = PDF_CS_CAL_GRAY;
            info->num_components = 1;

            if (cs_array->length >= 2) {
                Item dict_item = cs_array->items[1];
                if (get_type_id(dict_item) == LMD_TYPE_MAP) {
                    Map* dict = dict_item.map;

                    // Gamma (optional, default 1)
                    String* gamma_key = input_create_string(input, "Gamma");
                    Item gamma_item = {.item = map_get(dict, {.item = s2it(gamma_key)}).item};
                    if (gamma_item.item != ITEM_NULL) {
                        if (get_type_id(gamma_item) == LMD_TYPE_FLOAT) {
                            info->gamma[0] = gamma_item.get_double();
                        } else if (get_type_id(gamma_item) == LMD_TYPE_INT) {
                            info->gamma[0] = (double)gamma_item.int_val;
                        }
                    }
                    log_debug("CalGray color space: gamma=%.3f", info->gamma[0]);
                }
            }
            return info;
        }

        if (strcmp(type_name->chars, "CalRGB") == 0) {
            // [/CalRGB <<dict>>]
            info->type = PDF_CS_CAL_RGB;
            info->num_components = 3;

            if (cs_array->length >= 2) {
                Item dict_item = cs_array->items[1];
                if (get_type_id(dict_item) == LMD_TYPE_MAP) {
                    Map* dict = dict_item.map;

                    // Gamma array (optional, default [1 1 1])
                    String* gamma_key = input_create_string(input, "Gamma");
                    Item gamma_item = {.item = map_get(dict, {.item = s2it(gamma_key)}).item};
                    if (gamma_item.item != ITEM_NULL && get_type_id(gamma_item) == LMD_TYPE_ARRAY) {
                        Array* gamma_arr = gamma_item.array;
                        for (int i = 0; i < 3 && i < gamma_arr->length; i++) {
                            Item g = gamma_arr->items[i];
                            if (get_type_id(g) == LMD_TYPE_FLOAT) {
                                info->gamma[i] = g.get_double();
                            } else if (get_type_id(g) == LMD_TYPE_INT) {
                                info->gamma[i] = (double)g.int_val;
                            }
                        }
                    }
                    log_debug("CalRGB color space: gamma=[%.3f, %.3f, %.3f]",
                             info->gamma[0], info->gamma[1], info->gamma[2]);
                }
            }
            return info;
        }

        if (strcmp(type_name->chars, "Lab") == 0) {
            info->type = PDF_CS_LAB;
            info->num_components = 3;
            log_debug("Lab color space (limited support)");
            return info;
        }

        if (strcmp(type_name->chars, "Separation") == 0 ||
            strcmp(type_name->chars, "DeviceN") == 0) {
            // Separation and DeviceN require tint transform functions
            // For now, fall back to the alternate space or CMYK
            info->type = (strcmp(type_name->chars, "Separation") == 0) ?
                         PDF_CS_SEPARATION : PDF_CS_DEVICEN;
            info->num_components = 1;  // Separation has 1, DeviceN varies
            log_debug("%s color space (fallback to RGB)", type_name->chars);
            return info;
        }
    }

    // Default to DeviceRGB
    log_debug("Defaulting to DeviceRGB color space");
    info->type = PDF_CS_DEVICE_RGB;
    info->num_components = 3;
    return info;
}

/**
 * Look up a named color space from the resources dictionary
 */
static PDFColorSpaceInfo* lookup_named_colorspace(Input* input, Pool* pool,
                                                   const char* cs_name, Map* resources) {
    if (!resources || !cs_name) return nullptr;

    // Look in /ColorSpace dictionary of resources
    String* cs_key = input_create_string(input, "ColorSpace");
    Item cs_dict_item = {.item = map_get(resources, {.item = s2it(cs_key)}).item};

    if (cs_dict_item.item == ITEM_NULL || get_type_id(cs_dict_item) != LMD_TYPE_MAP) {
        return nullptr;
    }

    Map* cs_dict = cs_dict_item.map;

    // Look up the named color space
    String* name_key = input_create_string(input, cs_name);
    Item cs_item = {.item = map_get(cs_dict, {.item = s2it(name_key)}).item};

    if (cs_item.item == ITEM_NULL) {
        log_debug("Named color space '%s' not found in resources", cs_name);
        return nullptr;
    }

    log_debug("Found named color space '%s' in resources", cs_name);
    return parse_color_space(input, pool, cs_item, resources);
}

/**
 * Convert color components to RGB based on color space info
 */
static void apply_color_space_to_rgb(PDFColorSpaceInfo* cs_info, const double* components, double* rgb_out) {
    if (!cs_info || !components || !rgb_out) {
        rgb_out[0] = rgb_out[1] = rgb_out[2] = 0.0;
        return;
    }

    switch (cs_info->type) {
        case PDF_CS_DEVICE_RGB:
            rgb_out[0] = components[0];
            rgb_out[1] = components[1];
            rgb_out[2] = components[2];
            break;

        case PDF_CS_DEVICE_GRAY:
            rgb_out[0] = rgb_out[1] = rgb_out[2] = components[0];
            break;

        case PDF_CS_DEVICE_CMYK: {
            double c = components[0], m = components[1], y = components[2], k = components[3];
            rgb_out[0] = (1.0 - c) * (1.0 - k);
            rgb_out[1] = (1.0 - m) * (1.0 - k);
            rgb_out[2] = (1.0 - y) * (1.0 - k);
            break;
        }

        case PDF_CS_INDEXED: {
            // Look up index in palette
            int idx = (int)components[0];
            if (idx < 0) idx = 0;
            if (idx > cs_info->hival) idx = cs_info->hival;

            if (cs_info->lookup_table) {
                int base_components = 3;  // RGB default
                if (cs_info->base_type == PDF_CS_DEVICE_GRAY) base_components = 1;
                else if (cs_info->base_type == PDF_CS_DEVICE_CMYK) base_components = 4;

                int offset = idx * base_components;
                if (offset + base_components <= cs_info->lookup_table_size) {
                    if (cs_info->base_type == PDF_CS_DEVICE_RGB) {
                        rgb_out[0] = cs_info->lookup_table[offset] / 255.0;
                        rgb_out[1] = cs_info->lookup_table[offset + 1] / 255.0;
                        rgb_out[2] = cs_info->lookup_table[offset + 2] / 255.0;
                    } else if (cs_info->base_type == PDF_CS_DEVICE_GRAY) {
                        rgb_out[0] = rgb_out[1] = rgb_out[2] =
                            cs_info->lookup_table[offset] / 255.0;
                    } else if (cs_info->base_type == PDF_CS_DEVICE_CMYK) {
                        double c = cs_info->lookup_table[offset] / 255.0;
                        double m = cs_info->lookup_table[offset + 1] / 255.0;
                        double y = cs_info->lookup_table[offset + 2] / 255.0;
                        double k = cs_info->lookup_table[offset + 3] / 255.0;
                        rgb_out[0] = (1.0 - c) * (1.0 - k);
                        rgb_out[1] = (1.0 - m) * (1.0 - k);
                        rgb_out[2] = (1.0 - y) * (1.0 - k);
                    }
                }
            }
            break;
        }

        case PDF_CS_ICCBASED:
            // Simplified: treat as RGB, Gray, or CMYK based on N
            if (cs_info->icc_n == 1) {
                rgb_out[0] = rgb_out[1] = rgb_out[2] = components[0];
            } else if (cs_info->icc_n == 3) {
                rgb_out[0] = components[0];
                rgb_out[1] = components[1];
                rgb_out[2] = components[2];
            } else if (cs_info->icc_n == 4) {
                double c = components[0], m = components[1], y = components[2], k = components[3];
                rgb_out[0] = (1.0 - c) * (1.0 - k);
                rgb_out[1] = (1.0 - m) * (1.0 - k);
                rgb_out[2] = (1.0 - y) * (1.0 - k);
            } else {
                rgb_out[0] = rgb_out[1] = rgb_out[2] = 0.0;
            }
            break;

        case PDF_CS_CAL_GRAY: {
            // Apply gamma correction: out = in^gamma
            double gray = components[0];
            if (cs_info->gamma[0] != 1.0 && gray > 0) {
                gray = pow(gray, cs_info->gamma[0]);
            }
            rgb_out[0] = rgb_out[1] = rgb_out[2] = gray;
            break;
        }

        case PDF_CS_CAL_RGB: {
            // Apply gamma correction to each channel
            for (int i = 0; i < 3; i++) {
                double val = components[i];
                if (cs_info->gamma[i] != 1.0 && val > 0) {
                    val = pow(val, cs_info->gamma[i]);
                }
                rgb_out[i] = val;
            }
            break;
        }

        case PDF_CS_LAB:
            // Simplified Lab to RGB (approximate)
            // L* is in [0, 100], a* and b* are approximately [-128, 127]
            // For now, just use a simple approximation
            rgb_out[0] = components[0] / 100.0;  // Approximate
            rgb_out[1] = (components[1] + 128.0) / 255.0;
            rgb_out[2] = (components[2] + 128.0) / 255.0;
            break;

        case PDF_CS_SEPARATION:
        case PDF_CS_DEVICEN:
            // Simplified: treat as grayscale tint
            rgb_out[0] = rgb_out[1] = rgb_out[2] = 1.0 - components[0];
            break;

        case PDF_CS_PATTERN:
        default:
            rgb_out[0] = rgb_out[1] = rgb_out[2] = 0.0;
            break;
    }

    // Clamp values to [0, 1]
    for (int i = 0; i < 3; i++) {
        if (rgb_out[i] < 0.0) rgb_out[i] = 0.0;
        if (rgb_out[i] > 1.0) rgb_out[i] = 1.0;
    }
}

/**
 * Process a single PDF object
 */
static void process_pdf_object(Input* input, Pool* view_pool, ViewBlock* parent, Item obj_item) {
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
    if (obj_item._type_id != 0 && actual_type != LMD_TYPE_MAP) {
        log_debug("Skipping non-map object (type_id=%d, actual_type=%d)", obj_item._type_id, actual_type);
        return;
    }

    Map* obj_map = obj_item.map;

    // Check for type field
    String* type_key = input_create_string(input, "type");
    Item type_item = {.item = map_get(obj_map, {.item = s2it(type_key)}).item};

    if (type_item.item == ITEM_NULL) {
        log_debug("Object has no type field");
        return;
    }

    String* type_str = type_item.get_string();
    log_debug("Processing object of type: %s", type_str->chars);

    // Process stream objects
    if (strcmp(type_str->chars, "stream") == 0) {
        process_pdf_stream(input, view_pool, parent, obj_map, nullptr, nullptr);
    }
    // Process indirect objects
    else if (strcmp(type_str->chars, "indirect_object") == 0) {
        String* content_key = input_create_string(input, "content");
        Item content_item = {.item = map_get(obj_map, {.item = s2it(content_key)}).item};
        if (content_item.item != ITEM_NULL) {
            process_pdf_object(input, view_pool, parent, content_item);
        }
    }
}

/**
 * Look up font entry from resources and cache it for ToUnicode decoding
 * Called when Tf operator sets the current font
 */
static void lookup_font_entry(PDFStreamParser* parser, const char* font_name) {
    if (!parser || !parser->font_cache || !parser->resources || !font_name) {
        if (parser) parser->state.current_font_entry = nullptr;
        return;
    }

    // Check if already in cache
    PDFFontEntry* entry = pdf_font_cache_get(parser->font_cache, font_name);
    if (entry) {
        parser->state.current_font_entry = entry;
        log_debug("Using cached font entry for '%s' (tounicode=%d)",
                 font_name, entry->to_unicode_count);
        return;
    }

    Pool* pool = parser->input->pool;

    // Look up font dictionary from resources
    MarkBuilder builder(parser->input);
    String* font_key = builder.createString("Font");
    Item fonts_item = {.item = map_get(parser->resources, {.item = s2it(font_key)}).item};

    // Resolve indirect reference if needed
    if (fonts_item.item != ITEM_NULL && parser->pdf_data) {
        fonts_item = pdf_resolve_reference(parser->pdf_data, fonts_item, pool);
    }

    if (fonts_item.item == ITEM_NULL || get_type_id(fonts_item) != LMD_TYPE_MAP) {
        log_debug("No Font dictionary in resources for '%s' (type=%d)",
                 font_name, fonts_item.item != ITEM_NULL ? get_type_id(fonts_item) : -1);
        parser->state.current_font_entry = nullptr;
        return;
    }

    Map* fonts_dict = fonts_item.map;
    log_debug("Found Font dictionary with font keys for lookup of '%s'", font_name);

    // Look up specific font (e.g., F1)
    String* specific_font_key = builder.createString(font_name);
    Item font_item = {.item = map_get(fonts_dict, {.item = s2it(specific_font_key)}).item};

    // Resolve indirect reference if needed
    if (font_item.item != ITEM_NULL && parser->pdf_data) {
        font_item = pdf_resolve_reference(parser->pdf_data, font_item, pool);
    }

    if (font_item.item == ITEM_NULL || get_type_id(font_item) != LMD_TYPE_MAP) {
        log_debug("Font '%s' not found in resources (type=%d)",
                 font_name, font_item.item != ITEM_NULL ? get_type_id(font_item) : -1);
        parser->state.current_font_entry = nullptr;
        return;
    }

    log_debug("Found font '%s' dictionary, adding to cache", font_name);

    // Add to cache (this will parse ToUnicode CMap)
    entry = pdf_font_cache_add(parser->font_cache, font_name, font_item.map, parser->input, parser->pdf_data);
    parser->state.current_font_entry = entry;

    if (entry) {
        log_info("Cached font '%s' with ToUnicode mapping (%d entries)",
                font_name, entry->to_unicode_count);
    }
}

/**
 * Process a PDF content stream
 */
static void process_pdf_stream(Input* input, Pool* view_pool, ViewBlock* parent, Map* stream_map, Map* resources, Map* pdf_data) {
    log_debug("Processing PDF stream");

    // Get stream data
    String* data_key = input_create_string(input, "data");
    Item data_item = {.item = map_get(stream_map, {.item = s2it(data_key)}).item};

    if (data_item.item == ITEM_NULL) {
        log_warn("Stream has no data");
        return;
    }

    String* stream_data = data_item.get_string();
    const char* content_data = stream_data->chars;
    size_t content_len = stream_data->len;
    char* decompressed_data = nullptr;

    // Get stream dictionary (contains Length, Filter, DecodeParms, etc.)
    String* dict_key = input_create_string(input, "dictionary");
    Item dict_item = {.item = map_get(stream_map, {.item = s2it(dict_key)}).item};
    Map* stream_dict = (dict_item.item != ITEM_NULL) ? dict_item.map : nullptr;

    // Check if stream is compressed and decompress if needed
    if (stream_dict) {
        String* filter_key = input_create_string(input, "Filter");
        Item filter_item = {.item = map_get(stream_dict, {.item = s2it(filter_key)}).item};

        if (filter_item.item != ITEM_NULL) {
            // Get decode parameters if present
            String* decode_key = input_create_string(input, "DecodeParms");
            Item decode_item = {.item = map_get(stream_dict, {.item = s2it(decode_key)}).item};

            // Helper lambda to extract decode params from a dict
            auto extract_decode_params = [input](Map* params_dict, PDFDecodeParams* params) {
                pdf_decode_params_init(params);
                if (!params_dict) return;

                // Extract Predictor
                String* pred_key = input_create_string(input, "Predictor");
                Item pred_item = {.item = map_get(params_dict, {.item = s2it(pred_key)}).item};
                if (pred_item.item != ITEM_NULL) {
                    TypeId pred_type = get_type_id(pred_item);
                    if (pred_type == LMD_TYPE_FLOAT) {
                        params->predictor = (int)pred_item.get_double();
                    } else if (pred_type == LMD_TYPE_INT) {
                        params->predictor = pred_item.int_val;
                    }
                }

                // Extract Colors
                String* colors_key = input_create_string(input, "Colors");
                Item colors_item = {.item = map_get(params_dict, {.item = s2it(colors_key)}).item};
                if (colors_item.item != ITEM_NULL) {
                    TypeId colors_type = get_type_id(colors_item);
                    if (colors_type == LMD_TYPE_FLOAT) {
                        params->colors = (int)colors_item.get_double();
                    } else if (colors_type == LMD_TYPE_INT) {
                        params->colors = colors_item.int_val;
                    }
                }

                // Extract BitsPerComponent
                String* bpc_key = input_create_string(input, "BitsPerComponent");
                Item bpc_item = {.item = map_get(params_dict, {.item = s2it(bpc_key)}).item};
                if (bpc_item.item != ITEM_NULL) {
                    TypeId bpc_type = get_type_id(bpc_item);
                    if (bpc_type == LMD_TYPE_FLOAT) {
                        params->bits = (int)bpc_item.get_double();
                    } else if (bpc_type == LMD_TYPE_INT) {
                        params->bits = bpc_item.int_val;
                    }
                }

                // Extract Columns
                String* cols_key = input_create_string(input, "Columns");
                Item cols_item = {.item = map_get(params_dict, {.item = s2it(cols_key)}).item};
                if (cols_item.item != ITEM_NULL) {
                    TypeId cols_type = get_type_id(cols_item);
                    if (cols_type == LMD_TYPE_FLOAT) {
                        params->columns = (int)cols_item.get_double();
                    } else if (cols_type == LMD_TYPE_INT) {
                        params->columns = cols_item.int_val;
                    }
                }

                // Extract EarlyChange (for LZW)
                String* ec_key = input_create_string(input, "EarlyChange");
                Item ec_item = {.item = map_get(params_dict, {.item = s2it(ec_key)}).item};
                if (ec_item.item != ITEM_NULL) {
                    TypeId ec_type = get_type_id(ec_item);
                    if (ec_type == LMD_TYPE_FLOAT) {
                        params->early_change = (int)ec_item.get_double();
                    } else if (ec_type == LMD_TYPE_INT) {
                        params->early_change = ec_item.int_val;
                    }
                }

                log_debug("Decode params: predictor=%d, colors=%d, bits=%d, columns=%d",
                         params->predictor, params->colors, params->bits, params->columns);
            };

            // Get filter(s) - can be a single name or an array
            TypeId filter_type = get_type_id(filter_item);

            if (filter_type == LMD_TYPE_ARRAY) {
                // Multiple filters
                Array* filter_array = filter_item.array;
                const char** filters = (const char**)mem_alloc(sizeof(char*) * filter_array->length, MEM_CAT_INPUT_CSS);
                PDFDecodeParams* decode_params = (PDFDecodeParams*)mem_calloc(filter_array->length, sizeof(PDFDecodeParams), MEM_CAT_INPUT_CSS);

                if (filters && decode_params) {
                    for (int i = 0; i < filter_array->length; i++) {
                        Item filter_name_item = array_get(filter_array, i);
                        String* filter_name = filter_name_item.get_string();
                        filters[i] = filter_name->chars;

                        // Initialize with defaults
                        pdf_decode_params_init(&decode_params[i]);

                        // Get decode params if available (may be array or dict)
                        if (decode_item.item != ITEM_NULL) {
                            TypeId decode_type = get_type_id(decode_item);
                            if (decode_type == LMD_TYPE_ARRAY) {
                                Array* decode_array = decode_item.array;
                                if (i < decode_array->length) {
                                    Item param_item = array_get(decode_array, i);
                                    if (param_item.item != ITEM_NULL && get_type_id(param_item) == LMD_TYPE_MAP) {
                                        extract_decode_params(param_item.map, &decode_params[i]);
                                    }
                                }
                            } else if (decode_type == LMD_TYPE_MAP && i == 0) {
                                // Single decode params dict applies to first filter
                                extract_decode_params(decode_item.map, &decode_params[i]);
                            }
                        }
                    }

                    size_t decompressed_len = 0;
                    decompressed_data = pdf_decompress_stream_with_params(content_data, content_len,
                                                              filters, filter_array->length,
                                                              decode_params, &decompressed_len);
                    mem_free(filters);  // allocated with mem_alloc
                    mem_free(decode_params);  // allocated with mem_calloc

                    if (decompressed_data) {
                        content_data = decompressed_data;
                        content_len = decompressed_len;
                        log_info("Decompressed stream: %zu -> %zu bytes", stream_data->len, decompressed_len);
                    } else {
                        log_error("Failed to decompress stream with multiple filters");
                        return;
                    }
                } else {
                    if (filters) mem_free(filters);  // allocated with mem_alloc
                    if (decode_params) mem_free(decode_params);  // allocated with mem_calloc
                    return;
                }
            } else if (filter_type == LMD_TYPE_STRING) {
                // Single filter
                String* filter_name = filter_item.get_string();
                const char* filters[1] = { filter_name->chars };
                PDFDecodeParams decode_params[1];
                pdf_decode_params_init(&decode_params[0]);

                // Get decode params if present
                if (decode_item.item != ITEM_NULL && get_type_id(decode_item) == LMD_TYPE_MAP) {
                    extract_decode_params(decode_item.map, &decode_params[0]);
                }

                size_t decompressed_len = 0;
                decompressed_data = pdf_decompress_stream_with_params(content_data, content_len,
                                                          filters, 1,
                                                          decode_params, &decompressed_len);

                if (decompressed_data) {
                    content_data = decompressed_data;
                    content_len = decompressed_len;
                    log_info("Decompressed stream: %zu -> %zu bytes", stream_data->len, decompressed_len);
                } else {
                    log_error("Failed to decompress stream with filter: %s", filter_name->chars);
                    return;
                }
            }
        }
    }

    // Parse the content stream
    PDFStreamParser* parser = pdf_stream_parser_create(
        content_data,
        content_len,
        input->pool,
        input
    );

    if (!parser) {
        log_error("Failed to create stream parser");
        if (decompressed_data) {
            free(decompressed_data);  // from pdf_decompress_stream_with_params which uses stdlib
        }
        return;
    }

    // Set page resources for ExtGState lookup
    parser->resources = resources;

    // Set pdf_data for resolving indirect references
    parser->pdf_data = pdf_data;

    // Create font cache for ToUnicode decoding
    parser->font_cache = pdf_font_cache_create(input->pool);

    // Process operators
    PDFOperator* op;
    while ((op = pdf_parse_next_operator(parser)) != nullptr) {
        process_pdf_operator(input, view_pool, parent, parser, op);
    }

    pdf_stream_parser_destroy(parser);

    // Free decompressed data if allocated
    if (decompressed_data) {
        free(decompressed_data);  // from pdf_decompress_stream_with_params which uses stdlib
    }
}

/**
 * Process a single PDF operator
 */
static void process_pdf_operator(Input* input, Pool* view_pool, ViewBlock* parent,
                                 PDFStreamParser* parser, PDFOperator* op) {
    // Debug: log every operator type
    if (op->type == PDF_OP_gs) {
        log_info(">>> Processing PDF_OP_gs operator (type=%d)", op->type);
    }

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
            // Look up font entry for ToUnicode decoding
            lookup_font_entry(parser, op->operands.set_font.font_name->chars);
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
            create_text_view(input, view_pool, parent, parser, op->operands.show_text.text);
            break;

        case PDF_OP_TJ:
            // Show text array (with kerning adjustments)
            log_debug("Show text array");
            if (op->operands.text_array.array) {
                create_text_array_views(input, view_pool, parent, parser, op->operands.text_array.array);
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
            parser->state.fill_color[0] = op->operands.rgb_color.r;
            parser->state.fill_color[1] = op->operands.rgb_color.g;
            parser->state.fill_color[2] = op->operands.rgb_color.b;
            log_debug("State now: fill_color = [%.2f, %.2f, %.2f]",
                     parser->state.fill_color[0],
                     parser->state.fill_color[1],
                     parser->state.fill_color[2]);
            break;

        case PDF_OP_RG:
            // Set stroke color (RGB)
            log_debug("Set stroke color: %.2f %.2f %.2f",
                     op->operands.rgb_color.r,
                     op->operands.rgb_color.g,
                     op->operands.rgb_color.b);
            parser->state.stroke_color[0] = op->operands.rgb_color.r;
            parser->state.stroke_color[1] = op->operands.rgb_color.g;
            parser->state.stroke_color[2] = op->operands.rgb_color.b;
            break;

        // Path construction operators
        case PDF_OP_m:
            // Move to - start new subpath
            log_debug("Move to: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            // Initialize or reset path tracking
            parser->state.path_start_x = op->operands.text_position.tx;
            parser->state.path_start_y = op->operands.text_position.ty;
            parser->state.path_min_x = op->operands.text_position.tx;
            parser->state.path_min_y = op->operands.text_position.ty;
            parser->state.path_max_x = op->operands.text_position.tx;
            parser->state.path_max_y = op->operands.text_position.ty;
            parser->state.current_x = op->operands.text_position.tx;
            parser->state.current_y = op->operands.text_position.ty;
            parser->state.has_current_path = 1;
            break;

        case PDF_OP_l:
            // Line to - extend current path
            log_debug("Line to: %.2f, %.2f",
                     op->operands.text_position.tx,
                     op->operands.text_position.ty);
            if (parser->state.has_current_path) {
                // Update bounding box
                double x = op->operands.text_position.tx;
                double y = op->operands.text_position.ty;
                if (x < parser->state.path_min_x) parser->state.path_min_x = x;
                if (y < parser->state.path_min_y) parser->state.path_min_y = y;
                if (x > parser->state.path_max_x) parser->state.path_max_x = x;
                if (y > parser->state.path_max_y) parser->state.path_max_y = y;
                parser->state.current_x = x;
                parser->state.current_y = y;
            }
            break;

        case PDF_OP_c:
            // Cubic Bezier curve - extend current path
            log_debug("Curve to: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f",
                     op->operands.text_matrix.a, op->operands.text_matrix.b,
                     op->operands.text_matrix.c, op->operands.text_matrix.d,
                     op->operands.text_matrix.e, op->operands.text_matrix.f);
            if (parser->state.has_current_path) {
                // Update bounding box with end point (simplified - should include control points)
                double x = op->operands.text_matrix.e;
                double y = op->operands.text_matrix.f;
                if (x < parser->state.path_min_x) parser->state.path_min_x = x;
                if (y < parser->state.path_min_y) parser->state.path_min_y = y;
                if (x > parser->state.path_max_x) parser->state.path_max_x = x;
                if (y > parser->state.path_max_y) parser->state.path_max_y = y;
                parser->state.current_x = x;
                parser->state.current_y = y;
            }
            break;

        case PDF_OP_re:
            // Rectangle - store coordinates for later painting
            log_debug("Rectangle: %.2f, %.2f, %.2f x %.2f",
                     op->operands.rect.x, op->operands.rect.y,
                     op->operands.rect.width, op->operands.rect.height);
            // Store rectangle in parser state
            parser->state.current_rect_x = op->operands.rect.x;
            parser->state.current_rect_y = op->operands.rect.y;
            parser->state.current_rect_width = op->operands.rect.width;
            parser->state.current_rect_height = op->operands.rect.height;
            parser->state.has_current_rect = 1;
            // Also set general path tracking
            parser->state.path_min_x = op->operands.rect.x;
            parser->state.path_min_y = op->operands.rect.y;
            parser->state.path_max_x = op->operands.rect.x + op->operands.rect.width;
            parser->state.path_max_y = op->operands.rect.y + op->operands.rect.height;
            parser->state.has_current_path = 1;
            break;

        case PDF_OP_h:
            // Close path
            log_debug("Close path");
            break;

        // Path painting operators
        case PDF_OP_S:
            // Stroke path - uses ThorVG for curves/dashes, otherwise simple rect
            log_debug("Stroke path");
            if (needs_thorvg_path_render(parser)) {
                create_path_view_thorvg(input, view_pool, parent, parser, PAINT_STROKE_ONLY);
            } else {
                create_rect_view(input, view_pool, parent, parser, PAINT_STROKE_ONLY);
            }
            break;

        case PDF_OP_s:
            // Close and stroke path
            log_debug("Close and stroke path");
            if (needs_thorvg_path_render(parser)) {
                create_path_view_thorvg(input, view_pool, parent, parser, PAINT_STROKE_ONLY);
            } else {
                create_rect_view(input, view_pool, parent, parser, PAINT_STROKE_ONLY);
            }
            break;

        case PDF_OP_f:
        case PDF_OP_F:
            // Fill path
            create_rect_view(input, view_pool, parent, parser, PAINT_FILL_ONLY);
            break;

        case PDF_OP_f_star:
            // Fill path (even-odd)
            log_debug("Fill path (even-odd)");
            create_rect_view(input, view_pool, parent, parser, PAINT_FILL_ONLY);
            break;

        case PDF_OP_B:
        case PDF_OP_B_star:
            // Fill and stroke
            log_debug("Fill and stroke path");
            if (needs_thorvg_path_render(parser)) {
                create_path_view_thorvg(input, view_pool, parent, parser, PAINT_FILL_AND_STROKE);
            } else {
                create_rect_view(input, view_pool, parent, parser, PAINT_FILL_AND_STROKE);
            }
            break;

        case PDF_OP_b:
        case PDF_OP_b_star:
            // Close, fill and stroke
            log_debug("Close, fill and stroke path");
            if (needs_thorvg_path_render(parser)) {
                create_path_view_thorvg(input, view_pool, parent, parser, PAINT_FILL_AND_STROKE);
            } else {
                create_rect_view(input, view_pool, parent, parser, PAINT_FILL_AND_STROKE);
            }
            break;

        case PDF_OP_n:
            // End path without painting - clear path state
            log_debug("End path (no paint)");
            parser->state.has_current_rect = 0;
            parser->state.has_current_path = 0;
            pdf_clear_path_segments(&parser->state);
            break;

        case PDF_OP_gs:
            // Set graphics state from ExtGState dictionary
            log_debug("PDF_OP_gs case: text=%p, resources=%p",
                     (void*)op->operands.show_text.text, (void*)parser->resources);
            if (op->operands.show_text.text && parser->resources) {
                const char* gs_name = op->operands.show_text.text->chars;
                log_debug("Processing gs operator: %s", gs_name);

                // Look up ExtGState in resources
                ConstItem extgstate_item = ((Map*)parser->resources)->get("ExtGState");
                if (extgstate_item.item != ITEM_NULL && extgstate_item.type_id() == LMD_TYPE_MAP) {
                    Map* extgstate_dict = (Map*)extgstate_item.map;

                    // Look up the specific graphics state by name
                    ConstItem gs_item = extgstate_dict->get(gs_name);
                    if (gs_item.item != ITEM_NULL && gs_item.type_id() == LMD_TYPE_MAP) {
                        Map* gs_dict = (Map*)gs_item.map;

                        // Check for ca (fill alpha)
                        ConstItem ca_item = gs_dict->get("ca");
                        if (ca_item.item != ITEM_NULL) {
                            double ca = 1.0;
                            Item ca_mut = *(Item*)&ca_item;
                            if (ca_mut.type_id() == LMD_TYPE_FLOAT) {
                                ca = ca_mut.get_double();
                            } else if (ca_mut.type_id() == LMD_TYPE_INT) {
                                ca = (double)ca_mut.int_val;
                            }
                            parser->state.fill_alpha = ca;
                            log_debug("Set fill alpha (ca): %.2f", ca);
                        }

                        // Check for CA (stroke alpha)
                        ConstItem CA_item = gs_dict->get("CA");
                        if (CA_item.item != ITEM_NULL) {
                            double CA = 1.0;
                            Item CA_mut = *(Item*)&CA_item;
                            if (CA_mut.type_id() == LMD_TYPE_FLOAT) {
                                CA = CA_mut.get_double();
                            } else if (CA_mut.type_id() == LMD_TYPE_INT) {
                                CA = (double)CA_mut.int_val;
                            }
                            parser->state.stroke_alpha = CA;
                            log_debug("Set stroke alpha (CA): %.2f", CA);
                        }
                    } else {
                        log_debug("Graphics state '%s' not found in ExtGState", gs_name);
                    }
                } else {
                    log_debug("No ExtGState dictionary in resources");
                }
            }
            break;

        case PDF_OP_Do:
            // Invoke XObject (image or form)
            if (op->operands.show_text.text && parser->resources) {
                const char* xobject_name = op->operands.show_text.text->chars;
                log_debug("Do operator: invoking XObject '%s'", xobject_name);
                handle_do_operator(input, view_pool, parent, parser, xobject_name);
            }
            break;

        // Color space operators
        case PDF_OP_cs:
            // Set fill color space
            if (op->operands.show_text.text && parser->resources) {
                const char* cs_name = op->operands.show_text.text->chars;
                log_debug("cs operator: setting fill color space to '%s'", cs_name);

                // Look up named color space if not a device color space
                if (strcmp(cs_name, "DeviceRGB") != 0 &&
                    strcmp(cs_name, "DeviceGray") != 0 &&
                    strcmp(cs_name, "DeviceCMYK") != 0) {
                    PDFColorSpaceInfo* cs_info = lookup_named_colorspace(
                        input, input->pool, cs_name, (Map*)parser->resources);
                    if (cs_info) {
                        parser->state.fill_cs_info = cs_info;
                        parser->state.fill_color_space = cs_info->type;
                        log_debug("Resolved fill color space '%s' to type %d", cs_name, cs_info->type);
                    }
                }
            }
            break;

        case PDF_OP_CS:
            // Set stroke color space
            if (op->operands.show_text.text && parser->resources) {
                const char* cs_name = op->operands.show_text.text->chars;
                log_debug("CS operator: setting stroke color space to '%s'", cs_name);

                if (strcmp(cs_name, "DeviceRGB") != 0 &&
                    strcmp(cs_name, "DeviceGray") != 0 &&
                    strcmp(cs_name, "DeviceCMYK") != 0) {
                    PDFColorSpaceInfo* cs_info = lookup_named_colorspace(
                        input, input->pool, cs_name, (Map*)parser->resources);
                    if (cs_info) {
                        parser->state.stroke_cs_info = cs_info;
                        parser->state.stroke_color_space = cs_info->type;
                        log_debug("Resolved stroke color space '%s' to type %d", cs_name, cs_info->type);
                    }
                }
            }
            break;

        case PDF_OP_sc:
        case PDF_OP_scn:
            // Set fill color (components depend on current color space)
            if (parser->state.fill_cs_info) {
                // Use extended color space conversion
                double rgb[3];
                apply_color_space_to_rgb(parser->state.fill_cs_info,
                                        parser->state.fill_color_components, rgb);
                parser->state.fill_color[0] = rgb[0];
                parser->state.fill_color[1] = rgb[1];
                parser->state.fill_color[2] = rgb[2];
                log_debug("sc/scn: converted to RGB [%.3f, %.3f, %.3f]", rgb[0], rgb[1], rgb[2]);
            }
            // For device color spaces, conversion is already done in operators.cpp
            break;

        case PDF_OP_SC:
        case PDF_OP_SCN:
            // Set stroke color (components depend on current color space)
            if (parser->state.stroke_cs_info) {
                double rgb[3];
                apply_color_space_to_rgb(parser->state.stroke_cs_info,
                                        parser->state.stroke_color_components, rgb);
                parser->state.stroke_color[0] = rgb[0];
                parser->state.stroke_color[1] = rgb[1];
                parser->state.stroke_color[2] = rgb[2];
                log_debug("SC/SCN: converted to RGB [%.3f, %.3f, %.3f]", rgb[0], rgb[1], rgb[2]);
            }
            break;

        default:
            if (op->type == PDF_OP_gs) {
                log_error("PDF_OP_gs hit default case! op->type=%d, PDF_OP_gs=%d", op->type, PDF_OP_gs);
            }
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
static void create_rect_view(Input* input, Pool* view_pool, ViewBlock* parent,
                             PDFStreamParser* parser, PaintOperation paint_op) {
    // Check if we have a stored rectangle from 're' operator, or a general path
    if (!parser->state.has_current_rect && !parser->state.has_current_path) {
        log_debug("No rectangle or path stored, skipping view creation");
        return;
    }

    double x, y, width, height;

    // Use rectangle if available, otherwise use path bounding box
    if (parser->state.has_current_rect) {
        x = parser->state.current_rect_x;
        y = parser->state.current_rect_y;
        width = parser->state.current_rect_width;
        height = parser->state.current_rect_height;
    } else {
        // Use path bounding box
        x = parser->state.path_min_x;
        y = parser->state.path_min_y;
        width = parser->state.path_max_x - parser->state.path_min_x;
        height = parser->state.path_max_y - parser->state.path_min_y;

        // For stroke-only operations, give lines minimum thickness based on line_width
        if (paint_op == PAINT_STROKE_ONLY || paint_op == PAINT_FILL_AND_STROKE) {
            double line_width = parser->state.line_width > 0 ? parser->state.line_width : 1.0;
            // If width or height is 0, it's a line - give it thickness
            if (width < line_width) width = line_width;
            if (height < line_width) height = line_width;
        }

        log_debug("Using path bounding box: (%.2f, %.2f) %.2f x %.2f", x, y, width, height);
    }

    // Apply CTM transformation to the rectangle
    // CTM is [a b c d e f] where:
    // x' = a*x + c*y + e
    // y' = b*x + d*y + f
    double* ctm = parser->state.ctm;
    double tx = ctm[0] * x + ctm[2] * y + ctm[4];
    double ty = ctm[1] * x + ctm[3] * y + ctm[5];
    // Width and height are scaled by the CTM
    double tw = width * ctm[0];   // Simplified: assumes no rotation
    double th = height * ctm[3];  // Simplified: assumes no rotation

    log_debug("CTM transform: (%.2f, %.2f) -> (%.2f, %.2f), size %.2f x %.2f -> %.2f x %.2f",
             x, y, tx, ty, width, height, tw, th);

    // Create ViewBlock for the rectangle
    ViewBlock* rect_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!rect_view) {
        log_error("Failed to allocate rect view");
        return;
    }

    // Convert PDF coordinates (bottom-left origin, Y up) to screen coordinates (top-left origin, Y down)
    // For rectangles, PDF y is the bottom edge, so screen_y = page_height - pdf_y - height
    double screen_y = parent->height - ty - th;

    rect_view->view_type = RDT_VIEW_BLOCK;
    rect_view->x = (float)tx;
    rect_view->y = (float)screen_y;  // Screen Y (top-down)
    rect_view->width = (float)tw;
    rect_view->height = (float)th;

    log_debug("Created rect view at PDF(%.2f, %.2f) -> screen(%.2f, %.2f) size %.2f x %.2f",
             tx, ty, (float)tx, (float)screen_y, tw, th);

    // Create empty DomElement for styling
    DomElement* dom_elem = (DomElement*)pool_calloc(view_pool, sizeof(DomElement));
    if (dom_elem) {
        dom_elem->node_type = DOM_NODE_ELEMENT;
        dom_elem->tag_name = "div";  // Treat as div for layout
        dom_elem->parent = nullptr;
        dom_elem->next_sibling = nullptr;
        dom_elem->prev_sibling = nullptr;
        dom_elem->first_child = nullptr;
        dom_elem->doc = nullptr;  // PDF views don't have backing document
    }

    // Directly assign DomElement as the node
    // rect_view = dom_elem;

    // Apply fill color and/or stroke color from graphics state based on paint operation
    BoundaryProp* bound = nullptr;

    // Apply fill color only for fill or fill+stroke operations
    if ((paint_op == PAINT_FILL_ONLY || paint_op == PAINT_FILL_AND_STROKE) &&
        parser->state.fill_color[0] >= 0.0) {
        if (!bound) {
            bound = (BoundaryProp*)pool_calloc(view_pool, sizeof(BoundaryProp));
        }

        if (bound) {
            // Create background property
            BackgroundProp* bg = (BackgroundProp*)pool_calloc(view_pool, sizeof(BackgroundProp));
            if (bg) {
                // Convert PDF RGB (0.0-1.0) to Color (0-255)
                bg->color.r = (uint8_t)(parser->state.fill_color[0] * 255.0);
                bg->color.g = (uint8_t)(parser->state.fill_color[1] * 255.0);
                bg->color.b = (uint8_t)(parser->state.fill_color[2] * 255.0);
                // Apply fill alpha from graphics state
                bg->color.a = (uint8_t)(parser->state.fill_alpha * 255.0);
                // NOTE: Don't set bg->color.c - it's a union with r,g,b,a

                bound->background = bg;

                log_debug("Applied fill color: RGB(%d, %d, %d) alpha=%.2f",
                         bg->color.r, bg->color.g, bg->color.b, parser->state.fill_alpha);
            }
        }
    }

    // Apply stroke color only for stroke or fill+stroke operations
    if ((paint_op == PAINT_STROKE_ONLY || paint_op == PAINT_FILL_AND_STROKE) &&
        parser->state.stroke_color[0] >= 0.0) {
        if (!bound) {
            bound = (BoundaryProp*)pool_calloc(view_pool, sizeof(BoundaryProp));
        }

        if (bound) {
            // Create border property
            BorderProp* border = (BorderProp*)pool_calloc(view_pool, sizeof(BorderProp));
            if (border) {
                // Convert PDF RGB (0.0-1.0) to Color (0-255)
                Color stroke_color;
                stroke_color.r = (uint8_t)(parser->state.stroke_color[0] * 255.0);
                stroke_color.g = (uint8_t)(parser->state.stroke_color[1] * 255.0);
                stroke_color.b = (uint8_t)(parser->state.stroke_color[2] * 255.0);
                // Apply stroke alpha from graphics state
                stroke_color.a = (uint8_t)(parser->state.stroke_alpha * 255.0);
                // NOTE: Don't set stroke_color.c - it's a union with r,g,b,a

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
                border->top_style = CSS_VALUE_SOLID;
                border->right_style = CSS_VALUE_SOLID;
                border->bottom_style = CSS_VALUE_SOLID;
                border->left_style = CSS_VALUE_SOLID;

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

    // Clear both rectangle and general path state after using it
    parser->state.has_current_rect = 0;
    parser->state.has_current_path = 0;
    pdf_clear_path_segments(&parser->state);
}

/**
 * Check if path needs ThorVG rendering (has curves or dashes)
 */
static bool needs_thorvg_path_render(PDFStreamParser* parser) {
    // Check for curves in path segments
    for (PathSegment* seg = parser->state.path_segments; seg; seg = seg->next) {
        if (seg->type == PATH_SEG_CURVETO) {
            return true;
        }
    }
    // Check for dash pattern
    if (parser->state.dash_pattern && parser->state.dash_pattern_length > 0) {
        return true;
    }
    return false;
}

// Enum for line orientation detection
enum PathLineType {
    PATH_LINE_HORIZONTAL,
    PATH_LINE_VERTICAL,
    PATH_LINE_DIAGONAL,
    PATH_LINE_CURVE,
    PATH_LINE_COMPLEX
};

/**
 * Detect if the path is a simple horizontal or vertical line
 */
static PathLineType detect_line_type(PathSegment* segments) {
    // Count segments and detect curve
    int seg_count = 0;
    bool has_curve = false;
    PathSegment* first = nullptr;
    PathSegment* second = nullptr;

    for (PathSegment* seg = segments; seg; seg = seg->next) {
        if (seg->type == PATH_SEG_CLOSE) continue;
        seg_count++;
        if (seg->type == PATH_SEG_CURVETO) has_curve = true;
        if (seg_count == 1) first = seg;
        if (seg_count == 2) second = seg;
    }

    if (has_curve) return PATH_LINE_CURVE;
    if (seg_count > 2) return PATH_LINE_COMPLEX;
    if (seg_count != 2 || !first || !second) return PATH_LINE_COMPLEX;

    // Simple MOVETO + LINETO - check orientation
    double dx = fabs(second->x - first->x);
    double dy = fabs(second->y - first->y);

    const double epsilon = 0.1;  // tolerance for "same" coordinate

    if (dy < epsilon && dx > epsilon) {
        return PATH_LINE_HORIZONTAL;
    } else if (dx < epsilon && dy > epsilon) {
        return PATH_LINE_VERTICAL;
    } else {
        return PATH_LINE_DIAGONAL;
    }
}

/**
 * Create a ThorVG path view for complex paths (curves, dashed lines)
 * Uses ThorVG for rendering Bezier curves and dashed/dotted strokes
 */
static void create_path_view_thorvg(Input* input, Pool* view_pool, ViewBlock* parent,
                                    PDFStreamParser* parser, PaintOperation paint_op) {
    PathSegment* segments = parser->state.path_segments;
    if (!segments) {
        log_debug("No path segments for ThorVG rendering");
        return;
    }

    PathLineType line_type = detect_line_type(segments);
    log_debug("Creating ThorVG path view with dash_pattern_length=%d, line_type=%d",
             parser->state.dash_pattern_length, line_type);

    // Calculate bounding box from path segments
    double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
    for (PathSegment* seg = segments; seg; seg = seg->next) {
        if (seg->type == PATH_SEG_CLOSE) continue;
        if (seg->x < min_x) min_x = seg->x;
        if (seg->y < min_y) min_y = seg->y;
        if (seg->x > max_x) max_x = seg->x;
        if (seg->y > max_y) max_y = seg->y;
        if (seg->type == PATH_SEG_CURVETO) {
            // Include control points in bounding box
            if (seg->x1 < min_x) min_x = seg->x1;
            if (seg->y1 < min_y) min_y = seg->y1;
            if (seg->x1 > max_x) max_x = seg->x1;
            if (seg->y1 > max_y) max_y = seg->y1;
            if (seg->x2 < min_x) min_x = seg->x2;
            if (seg->y2 < min_y) min_y = seg->y2;
            if (seg->x2 > max_x) max_x = seg->x2;
            if (seg->y2 > max_y) max_y = seg->y2;
        }
    }

    // Apply CTM transformation to bounding box
    double* ctm = parser->state.ctm;
    double line_width = parser->state.line_width > 0 ? parser->state.line_width : 1.0;

    // Transform min/max points
    double tx_min = ctm[0] * min_x + ctm[2] * min_y + ctm[4];
    double ty_min = ctm[1] * min_x + ctm[3] * min_y + ctm[5];
    double tx_max = ctm[0] * max_x + ctm[2] * max_y + ctm[4];
    double ty_max = ctm[1] * max_x + ctm[3] * max_y + ctm[5];

    // For horizontal/vertical lines, don't expand the thin dimension
    double width, height, pdf_x, pdf_y;
    if (line_type == PATH_LINE_HORIZONTAL) {
        // Horizontal line - full width, minimal height
        width = tx_max - tx_min;
        height = line_width;
        pdf_x = tx_min;
        pdf_y = ty_min - line_width / 2;
    } else if (line_type == PATH_LINE_VERTICAL) {
        // Vertical line - minimal width, full height
        width = line_width;
        height = ty_max - ty_min;
        pdf_x = tx_min - line_width / 2;
        pdf_y = ty_min;
    } else {
        // Complex path - expand bounding box for stroke width
        width = tx_max - tx_min + line_width * 2;
        height = ty_max - ty_min + line_width * 2;
        pdf_x = tx_min - line_width;
        pdf_y = ty_min - line_width;
    }

    log_debug("ThorVG path bounds: (%.2f, %.2f) -> (%.2f, %.2f), size %.2f x %.2f, type=%d",
             min_x, min_y, max_x, max_y, width, height, line_type);

    // Create ViewBlock for the path (container for ThorVG rendering)
    ViewBlock* path_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!path_view) {
        log_error("Failed to allocate path view");
        return;
    }

    // Convert PDF coordinates to screen coordinates
    double screen_y = parent->height - pdf_y - height;

    path_view->view_type = RDT_VIEW_BLOCK;
    path_view->x = (float)pdf_x;
    path_view->y = (float)screen_y;
    path_view->width = (float)width;
    path_view->height = (float)height;

    // Store path rendering info in a custom property
    // For curves and dashed/dotted lines, use VectorPath for ThorVG rendering
    // For simple solid lines, use single-sided border

    // Check if we need VectorPath rendering (curves OR dashed/dotted)
    bool has_dash_pattern = (parser->state.dash_pattern && parser->state.dash_pattern_length > 0);
    bool use_vector_path = (line_type == PATH_LINE_CURVE) || has_dash_pattern;

    // For curves and dashed/dotted lines, create VectorPathProp with path segments for ThorVG rendering
    if (use_vector_path) {
        log_debug("VectorPath rendering: line_type=%d, has_dash=%d", line_type, has_dash_pattern);

        // path_view is at (pdf_x, screen_y), so segment coordinates should be relative to this
        float view_origin_x = (float)pdf_x;
        float view_origin_y = (float)(parent->height - pdf_y - height);  // screen_y

        VectorPathProp* vpath = (VectorPathProp*)pool_calloc(view_pool, sizeof(VectorPathProp));
        if (vpath) {
            // Copy path segments (transformed to screen coordinates RELATIVE to path_view position)
            VectorPathSegment* last_vseg = nullptr;
            for (PathSegment* seg = segments; seg; seg = seg->next) {
                VectorPathSegment* vseg = (VectorPathSegment*)pool_calloc(view_pool, sizeof(VectorPathSegment));
                if (!vseg) break;

                // Transform coordinates using CTM
                double tx = ctm[0] * seg->x + ctm[2] * seg->y + ctm[4];
                double ty = ctm[1] * seg->x + ctm[3] * seg->y + ctm[5];

                // Convert to screen coordinates and make relative to path_view position
                float abs_screen_x = (float)tx;
                float abs_screen_y = (float)(parent->height - ty);
                vseg->x = abs_screen_x - view_origin_x;
                vseg->y = abs_screen_y - view_origin_y;

                if (seg->type == PATH_SEG_MOVETO) {
                    vseg->type = VectorPathSegment::VPATH_MOVETO;
                } else if (seg->type == PATH_SEG_LINETO) {
                    vseg->type = VectorPathSegment::VPATH_LINETO;
                } else if (seg->type == PATH_SEG_CURVETO) {
                    vseg->type = VectorPathSegment::VPATH_CURVETO;
                    // Transform control points
                    double tx1 = ctm[0] * seg->x1 + ctm[2] * seg->y1 + ctm[4];
                    double ty1 = ctm[1] * seg->x1 + ctm[3] * seg->y1 + ctm[5];
                    double tx2 = ctm[0] * seg->x2 + ctm[2] * seg->y2 + ctm[4];
                    double ty2 = ctm[1] * seg->x2 + ctm[3] * seg->y2 + ctm[5];
                    vseg->x1 = (float)tx1 - view_origin_x;
                    vseg->y1 = (float)(parent->height - ty1) - view_origin_y;
                    vseg->x2 = (float)tx2 - view_origin_x;
                    vseg->y2 = (float)(parent->height - ty2) - view_origin_y;
                } else if (seg->type == PATH_SEG_CLOSE) {
                    vseg->type = VectorPathSegment::VPATH_CLOSE;
                }

                // Link segments
                if (!vpath->segments) {
                    vpath->segments = vseg;
                } else if (last_vseg) {
                    last_vseg->next = vseg;
                }
                last_vseg = vseg;
            }

            // Set stroke properties
            vpath->has_stroke = (paint_op == PAINT_STROKE_ONLY || paint_op == PAINT_FILL_AND_STROKE);
            vpath->has_fill = (paint_op == PAINT_FILL_ONLY || paint_op == PAINT_FILL_AND_STROKE);

            if (vpath->has_stroke) {
                vpath->stroke_color.r = (uint8_t)(parser->state.stroke_color[0] * 255.0);
                vpath->stroke_color.g = (uint8_t)(parser->state.stroke_color[1] * 255.0);
                vpath->stroke_color.b = (uint8_t)(parser->state.stroke_color[2] * 255.0);
                vpath->stroke_color.a = 255;
            }
            if (vpath->has_fill) {
                vpath->fill_color.r = (uint8_t)(parser->state.fill_color[0] * 255.0);
                vpath->fill_color.g = (uint8_t)(parser->state.fill_color[1] * 255.0);
                vpath->fill_color.b = (uint8_t)(parser->state.fill_color[2] * 255.0);
                vpath->fill_color.a = 255;
            }
            vpath->stroke_width = (float)line_width;

            // Copy dash pattern if present
            if (parser->state.dash_pattern && parser->state.dash_pattern_length > 0) {
                vpath->dash_pattern = (float*)pool_calloc(view_pool,
                    sizeof(float) * parser->state.dash_pattern_length);
                if (vpath->dash_pattern) {
                    for (int i = 0; i < parser->state.dash_pattern_length; i++) {
                        vpath->dash_pattern[i] = (float)parser->state.dash_pattern[i];
                    }
                    vpath->dash_pattern_length = parser->state.dash_pattern_length;
                    log_debug("Copied dash pattern: length=%d, first=%.1f",
                             vpath->dash_pattern_length, vpath->dash_pattern[0]);
                }
            }

            path_view->vpath = vpath;

            log_info("Set vpath on path_view: vpath=%p, segments=%p, has_stroke=%d",
                    (void*)vpath, (void*)vpath->segments, vpath->has_stroke);

            log_debug("Created VectorPathProp with stroke=(%d,%d,%d), width=%.1f",
                     vpath->stroke_color.r, vpath->stroke_color.g, vpath->stroke_color.b,
                     vpath->stroke_width);
        }

        // Add to parent
        append_child_view((View*)parent, (View*)path_view);

        log_debug("Created curve path view at (%.2f, %.2f) size %.2f x %.2f",
                 pdf_x, screen_y, width, height);

        // Clear path state
        parser->state.has_current_rect = 0;
        parser->state.has_current_path = 0;
        pdf_clear_path_segments(&parser->state);
        return;
    }

    // Apply stroke color for stroke operations
    if (paint_op == PAINT_STROKE_ONLY || paint_op == PAINT_FILL_AND_STROKE) {
        BoundaryProp* bound = (BoundaryProp*)pool_calloc(view_pool, sizeof(BoundaryProp));
        if (bound) {
            BorderProp* border = (BorderProp*)pool_calloc(view_pool, sizeof(BorderProp));
            if (border) {
                Color stroke_color;
                stroke_color.r = (uint8_t)(parser->state.stroke_color[0] * 255.0);
                stroke_color.g = (uint8_t)(parser->state.stroke_color[1] * 255.0);
                stroke_color.b = (uint8_t)(parser->state.stroke_color[2] * 255.0);
                stroke_color.a = 255;

                // Set border style based on dash pattern
                CssEnum style = CSS_VALUE_SOLID;
                if (parser->state.dash_pattern && parser->state.dash_pattern_length > 0) {
                    double dash_len = parser->state.dash_pattern[0];
                    if (dash_len <= 2.0) {
                        style = CSS_VALUE_DOTTED;
                    } else {
                        style = CSS_VALUE_DASHED;
                    }
                }

                // Apply border only to appropriate sides based on line type
                if (line_type == PATH_LINE_HORIZONTAL) {
                    // Horizontal line - only apply top border
                    border->top_color = stroke_color;
                    border->width.top = (float)line_width;
                    border->top_style = style;
                    // Other sides: zero width
                    border->width.right = 0;
                    border->width.bottom = 0;
                    border->width.left = 0;
                    border->right_style = CSS_VALUE_NONE;
                    border->bottom_style = CSS_VALUE_NONE;
                    border->left_style = CSS_VALUE_NONE;
                } else if (line_type == PATH_LINE_VERTICAL) {
                    // Vertical line - only apply left border
                    border->left_color = stroke_color;
                    border->width.left = (float)line_width;
                    border->left_style = style;
                    // Other sides: zero width
                    border->width.top = 0;
                    border->width.right = 0;
                    border->width.bottom = 0;
                    border->top_style = CSS_VALUE_NONE;
                    border->right_style = CSS_VALUE_NONE;
                    border->bottom_style = CSS_VALUE_NONE;
                } else {
                    // Diagonal or complex - apply to all sides (approximation)
                    border->top_color = stroke_color;
                    border->right_color = stroke_color;
                    border->bottom_color = stroke_color;
                    border->left_color = stroke_color;
                    border->width.top = (float)line_width;
                    border->width.right = (float)line_width;
                    border->width.bottom = (float)line_width;
                    border->width.left = (float)line_width;
                    border->top_style = style;
                    border->right_style = style;
                    border->bottom_style = style;
                    border->left_style = style;
                }

                bound->border = border;
                log_debug("Applied ThorVG path stroke: RGB(%d, %d, %d), style=%d",
                         stroke_color.r, stroke_color.g, stroke_color.b, style);
            }
            path_view->bound = bound;
        }
    }

    // Add to parent
    append_child_view((View*)parent, (View*)path_view);

    log_debug("Created ThorVG path view at (%.2f, %.2f) size %.2f x %.2f",
             pdf_x, screen_y, width, height);

    // Clear path state
    parser->state.has_current_rect = 0;
    parser->state.has_current_path = 0;
    pdf_clear_path_segments(&parser->state);
}

/**
 * Create a ViewText node from PDF text
 * Creates TextRect for unified rendering with HTML text
 */
static void create_text_view(Input* input, Pool* view_pool, ViewBlock* parent,
                            PDFStreamParser* parser, String* text) {
    if (!text || text->len == 0) return;

    // Decode text using ToUnicode CMap or font encoding if available
    const char* display_text = text->chars;
    int display_len = text->len;
    char* decoded_text = nullptr;

    if (parser->state.current_font_entry &&
        pdf_font_needs_decoding(parser->state.current_font_entry)) {
        // Allocate buffer for decoded text (UTF-8 can be up to 4x the length)
        decoded_text = (char*)pool_calloc(view_pool, text->len * 4 + 1);
        if (decoded_text) {
            int decoded_len = pdf_font_decode_text(
                parser->state.current_font_entry,
                text->chars, text->len,
                decoded_text, text->len * 4 + 1);
            if (decoded_len > 0) {
                display_text = decoded_text;
                display_len = decoded_len;
                log_debug("Decoded text: '%s' -> '%s'",
                         text->chars, decoded_text);
            }
        }
    }

    // Calculate position from text matrix
    double x = parser->state.tm[4];  // e component (x translation)
    double y = parser->state.tm[5];  // f component (y translation)

    // Calculate effective font size from text matrix
    // The text matrix encodes the font scaling. The effective font size is:
    // font_size * sqrt(tm[0]^2 + tm[1]^2) for the horizontal component
    // For typical PDFs without rotation, this simplifies to font_size * abs(tm[0])
    double tm_scale = sqrt(parser->state.tm[0] * parser->state.tm[0] +
                          parser->state.tm[1] * parser->state.tm[1]);
    double effective_font_size = parser->state.font_size * tm_scale;

    // Ensure minimum font size
    if (effective_font_size < 1.0) effective_font_size = 12.0;

    // Convert PDF coordinates to screen coordinates
    // Check if the text matrix has a negative Y scale (tm[3] < 0), which means
    // the PDF is using a top-down coordinate system (like screen coordinates)
    // In this case, y is already measured from the top, so don't flip
    double screen_y;
    if (parser->state.tm[3] < 0) {
        // Negative Y scale means top-down coordinates - use y directly
        screen_y = y;
    } else {
        // Standard PDF coordinates (bottom-up) - flip Y
        // Screen y = page_height - pdf_y
        screen_y = parent->height - y;
    }

    // Create ViewText (which extends DomText, so it inherits text fields)
    ViewText* text_view = (ViewText*)pool_calloc(view_pool, sizeof(ViewText));
    if (!text_view) {
        log_error("Failed to allocate text view");
        return;
    }

    text_view->view_type = RDT_VIEW_TEXT;
    text_view->x = 0;  // Position is now in TextRect, not on ViewText
    text_view->y = 0;
    text_view->width = 0;  // Will be calculated during rendering
    text_view->height = (float)effective_font_size;

    // Set DomText fields directly on ViewText (since ViewText extends DomText)
    text_view->node_type = DOM_NODE_TEXT;
    text_view->text = display_text;  // Use decoded text
    text_view->length = display_len;
    text_view->native_string = decoded_text ? nullptr : text;  // Only reference original if not decoded
    text_view->content_type = DOM_TEXT_STRING;

    // Create TextRect for unified rendering with HTML text
    // TextRect contains position relative to the page (parent block)
    TextRect* rect = (TextRect*)pool_calloc(view_pool, sizeof(TextRect));
    if (rect) {
        rect->x = (float)x;
        rect->y = (float)screen_y;
        rect->width = 0;   // Width will be calculated during rendering
        rect->height = (float)effective_font_size;
        rect->start_index = 0;
        rect->length = display_len;  // Use decoded length
        rect->next = nullptr;
        text_view->rect = rect;
    }

    log_debug("Created text view at PDF(%.2f, %.2f) -> screen(%.2f, %.2f) font_size=%.2f: '%s'",
             x, y, (float)x, (float)screen_y, effective_font_size, display_text);

    // Create font property using proper font descriptor parsing
    if (parser->state.font_name) {
        FontProp* font = create_font_from_pdf(view_pool,
                                              parser->state.font_name->chars,
                                              effective_font_size);
        if (font) {
            text_view->font = font;
        }
    }

    // Apply text color from graphics state fill color
    text_view->color.r = (uint8_t)(parser->state.fill_color[0] * 255.0);
    text_view->color.g = (uint8_t)(parser->state.fill_color[1] * 255.0);
    text_view->color.b = (uint8_t)(parser->state.fill_color[2] * 255.0);
    text_view->color.a = 255; // Fully opaque

    log_debug("Applied text color: r=%u, g=%u, b=%u (RGB)",
             (unsigned)text_view->color.r,
             (unsigned)text_view->color.g,
             (unsigned)text_view->color.b);

    // Add to parent
    append_child_view((View*)parent, (View*)text_view);
}

/**
 * Create a ViewText node from pre-decoded text (already UTF-8)
 * This is used when text has already been decoded by the caller (e.g., from TJ array combining)
 */
static void create_text_view_raw(Input* input, Pool* view_pool, ViewBlock* parent,
                                 PDFStreamParser* parser, String* text) {
    if (!text || text->len == 0) return;

    // Text is already decoded - use directly
    const char* display_text = text->chars;
    int display_len = text->len;

    // Calculate position from text matrix
    double x = parser->state.tm[4];  // e component (x translation)
    double y = parser->state.tm[5];  // f component (y translation)

    // Calculate effective font size from text matrix
    double tm_scale = sqrt(parser->state.tm[0] * parser->state.tm[0] +
                          parser->state.tm[1] * parser->state.tm[1]);
    double effective_font_size = parser->state.font_size * tm_scale;
    if (effective_font_size < 1.0) effective_font_size = 12.0;

    // Convert PDF coordinates to screen coordinates
    double screen_y;
    if (parser->state.tm[3] < 0) {
        screen_y = y;
    } else {
        screen_y = parent->height - y;
    }

    // Create ViewText
    ViewText* text_view = (ViewText*)pool_calloc(view_pool, sizeof(ViewText));
    if (!text_view) {
        log_error("Failed to allocate text view");
        return;
    }

    text_view->view_type = RDT_VIEW_TEXT;
    text_view->x = 0;
    text_view->y = 0;
    text_view->width = 0;
    text_view->height = (float)effective_font_size;

    // Set DomText fields
    text_view->node_type = DOM_NODE_TEXT;
    text_view->text = display_text;
    text_view->length = display_len;
    text_view->native_string = nullptr;  // Text is pool-allocated
    text_view->content_type = DOM_TEXT_STRING;

    // Create TextRect
    TextRect* rect = (TextRect*)pool_calloc(view_pool, sizeof(TextRect));
    if (rect) {
        rect->x = (float)x;
        rect->y = (float)screen_y;
        rect->width = 0;
        rect->height = (float)effective_font_size;
        rect->start_index = 0;
        rect->length = display_len;
        rect->next = nullptr;
        text_view->rect = rect;
    }

    log_debug("Created text view at PDF(%.2f, %.2f) -> screen(%.2f, %.2f) font_size=%.2f: '%s'",
             x, y, (float)x, (float)screen_y, effective_font_size, display_text);

    // Create font property
    if (parser->state.font_name) {
        FontProp* font = create_font_from_pdf(view_pool,
                                              parser->state.font_name->chars,
                                              effective_font_size);
        if (font) {
            text_view->font = font;
        }
    }

    // Apply text color
    text_view->color.r = (uint8_t)(parser->state.fill_color[0] * 255.0);
    text_view->color.g = (uint8_t)(parser->state.fill_color[1] * 255.0);
    text_view->color.b = (uint8_t)(parser->state.fill_color[2] * 255.0);
    text_view->color.a = 255;

    // Add to parent
    append_child_view((View*)parent, (View*)text_view);
}

/**
 * Create ViewText nodes from TJ operator text array
 * TJ array format: [(string) num (string) num ...] where num is horizontal displacement in 1/1000 em
 *
 * Strategy: Combine adjacent strings with small kerning adjustments.
 * When a large spacing adjustment is encountered (word boundary), flush the current
 * accumulated text as a view and start a new accumulation.
 *
 * Threshold: -1000 (1 em) is typically used for word spacing in justified text.
 * Adjustments smaller than this threshold are considered intra-word kerning.
 */
static void create_text_array_views(Input* input, Pool* view_pool, ViewBlock* parent,
                                    PDFStreamParser* parser, Array* text_array) {
    if (!text_array || text_array->length == 0) return;

    // Calculate effective font size from text matrix for scaling
    double tm_scale = sqrt(parser->state.tm[0] * parser->state.tm[0] +
                          parser->state.tm[1] * parser->state.tm[1]);
    double effective_font_size = parser->state.font_size * tm_scale;
    if (effective_font_size < 1.0) effective_font_size = 12.0;

    // Threshold for word boundary detection (in 1/1000 em)
    // pdf.js seems to use a higher threshold, only breaking at very large gaps
    // Typical justified text uses around -300 to -500 for word spacing
    // Only break when gap is very large (e.g., column separation or manual spacing)
    const double WORD_BOUNDARY_THRESHOLD = -600.0;  // Negative = rightward spacing

    // Buffer for accumulating text
    size_t buffer_capacity = 4096;
    char* buffer = (char*)pool_calloc(view_pool, buffer_capacity);
    if (!buffer) return;
    size_t buffer_pos = 0;

    // Track starting position for the current text segment
    double segment_start_x = parser->state.tm[4];
    double segment_start_y = parser->state.tm[5];
    bool has_content = false;

    auto flush_buffer = [&]() {
        if (buffer_pos > 0) {
            // Trim trailing spaces
            while (buffer_pos > 0 && (buffer[buffer_pos - 1] == ' ' ||
                                       buffer[buffer_pos - 1] == '\t')) {
                buffer_pos--;
            }

            if (buffer_pos > 0) {
                buffer[buffer_pos] = '\0';
                String* str = create_string(view_pool, buffer);
                if (str) {
                    // Temporarily set position to segment start
                    double saved_x = parser->state.tm[4];
                    double saved_y = parser->state.tm[5];
                    parser->state.tm[4] = segment_start_x;
                    parser->state.tm[5] = segment_start_y;

                    create_text_view_raw(input, view_pool, parent, parser, str);

                    parser->state.tm[4] = saved_x;
                    parser->state.tm[5] = saved_y;
                }
            }
            buffer_pos = 0;
            has_content = false;
        }
    };

    for (int i = 0; i < text_array->length; i++) {
        Item item = text_array->items[i];
        TypeId type = item.type_id();

        if (type == LMD_TYPE_STRING) {
            String* text = item.get_string();
            if (text && text->len > 0) {
                // Start new segment if this is first content
                if (!has_content) {
                    segment_start_x = parser->state.tm[4];
                    segment_start_y = parser->state.tm[5];
                    has_content = true;
                }

                // Decode and add to buffer
                if (parser->state.current_font_entry &&
                    pdf_font_needs_decoding(parser->state.current_font_entry)) {
                    char* decode_buf = (char*)pool_calloc(view_pool, text->len * 4 + 1);
                    if (decode_buf) {
                        int decoded_len = pdf_font_decode_text(
                            parser->state.current_font_entry,
                            text->chars, text->len,
                            decode_buf, text->len * 4 + 1);
                        if (decoded_len > 0 && buffer_pos + decoded_len < buffer_capacity - 1) {
                            memcpy(buffer + buffer_pos, decode_buf, decoded_len);
                            buffer_pos += decoded_len;
                        }
                    }
                } else {
                    // No decoding needed
                    if (buffer_pos + text->len < buffer_capacity - 1) {
                        memcpy(buffer + buffer_pos, text->chars, text->len);
                        buffer_pos += text->len;
                    }
                }

                // Advance text position
                double text_width = text->len * effective_font_size * 0.5;
                parser->state.tm[4] += text_width;
            }
        }
        else if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) {
            // Kerning adjustment in 1/1000 em
            double adjustment = 0.0;
            if (type == LMD_TYPE_INT) {
                adjustment = (double)item.int_val;
            } else {
                adjustment = item.get_double();
            }

            // Check if this is a word boundary
            if (adjustment < WORD_BOUNDARY_THRESHOLD) {
                // Flush current buffer and start new segment
                flush_buffer();
            }

            // Apply position adjustment
            double displacement = -adjustment / 1000.0 * effective_font_size;
            parser->state.tm[4] += displacement;
        }
    }

    // Flush remaining content
    flush_buffer();

    log_debug("Processed TJ array with %lld elements", text_array->length);
}

/**
 * Append a child view to a parent view
 */
static void append_child_view(View* parent, View* child) {
    if (!parent || !child) return;

    ViewElement* parent_group = (ViewElement*)parent;

    // Set parent reference
    child->parent = parent_group;

    // Append to child list
    if (!parent_group->first_child) {
        parent_group->first_child = child;
    } else {
        View* last = parent_group->first_child;
        while (last->next_sibling) {
            last = last->next_sibling;
        }
        last->next_sibling = child;
    }
}

/**
 * Handle the Do operator - invoke an XObject (image or form)
 *
 * XObjects are external objects referenced by name from the Resources dictionary.
 * They can be:
 * - Image XObjects: embedded images (JPEG, JPEG2000, etc.)
 * - Form XObjects: reusable content streams (like nested PDF pages)
 */
static void handle_do_operator(Input* input, Pool* view_pool, ViewBlock* parent,
                               PDFStreamParser* parser, const char* xobject_name) {
    if (!parser->resources) {
        log_warn("handle_do_operator: No resources available");
        return;
    }

    // Look up XObject dictionary in resources
    ConstItem xobject_dict_item = ((Map*)parser->resources)->get("XObject");
    if (xobject_dict_item.item == ITEM_NULL || xobject_dict_item.type_id() != LMD_TYPE_MAP) {
        log_debug("No XObject dictionary in resources");
        return;
    }

    Map* xobject_dict = (Map*)xobject_dict_item.map;

    // Look up the specific XObject by name
    ConstItem xobj_item = xobject_dict->get(xobject_name);
    if (xobj_item.item == ITEM_NULL) {
        log_warn("XObject '%s' not found in resources", xobject_name);
        return;
    }

    // XObject should be a stream (map with stream_data key)
    if (xobj_item.type_id() != LMD_TYPE_MAP) {
        log_warn("XObject '%s' is not a dictionary", xobject_name);
        return;
    }

    Map* xobj = (Map*)xobj_item.map;

    // Get the XObject subtype to determine if it's an Image or Form
    ConstItem subtype_item = xobj->get("Subtype");
    if (subtype_item.item == ITEM_NULL) {
        log_warn("XObject '%s' has no Subtype", xobject_name);
        return;
    }

    const char* subtype = nullptr;
    if (subtype_item.type_id() == LMD_TYPE_STRING) {
        String* subtype_str = subtype_item.string();
        if (subtype_str) subtype = subtype_str->chars;
    }

    if (!subtype) {
        log_warn("XObject '%s' has invalid Subtype", xobject_name);
        return;
    }

    log_debug("XObject '%s' subtype: %s", xobject_name, subtype);

    if (strcmp(subtype, "Image") == 0) {
        // Handle Image XObject
        handle_image_xobject(input, view_pool, parent, parser, xobj, xobject_name);
    } else if (strcmp(subtype, "Form") == 0) {
        // Handle Form XObject (nested content stream)
        handle_form_xobject(input, view_pool, parent, parser, xobj, xobject_name);
    } else {
        log_debug("Unsupported XObject subtype: %s", subtype);
    }
}

/**
 * Handle Image XObject - extract and render embedded image
 */
static void handle_image_xobject(Input* input, Pool* view_pool, ViewBlock* parent,
                                  PDFStreamParser* parser, Map* image_dict, const char* name) {
    // Get image dimensions
    ConstItem width_item = image_dict->get("Width");
    ConstItem height_item = image_dict->get("Height");

    if (width_item.item == ITEM_NULL || height_item.item == ITEM_NULL) {
        log_warn("Image '%s' missing Width/Height", name);
        return;
    }

    int img_width = 0, img_height = 0;
    Item w_mut = *(Item*)&width_item;
    Item h_mut = *(Item*)&height_item;

    if (w_mut.type_id() == LMD_TYPE_INT) img_width = w_mut.int_val;
    else if (w_mut.type_id() == LMD_TYPE_FLOAT) img_width = (int)w_mut.get_double();

    if (h_mut.type_id() == LMD_TYPE_INT) img_height = h_mut.int_val;
    else if (h_mut.type_id() == LMD_TYPE_FLOAT) img_height = (int)h_mut.get_double();

    log_debug("Image '%s': %dx%d", name, img_width, img_height);

    // Get image data stream
    ConstItem stream_data_item = image_dict->get("stream_data");
    if (stream_data_item.item == ITEM_NULL) {
        log_warn("Image '%s' has no stream_data", name);
        return;
    }

    // Get bits per component and color space
    int bits_per_component = 8;
    ConstItem bpc_item = image_dict->get("BitsPerComponent");
    if (bpc_item.item != ITEM_NULL) {
        Item bpc_mut = *(Item*)&bpc_item;
        if (bpc_mut.type_id() == LMD_TYPE_INT) bits_per_component = bpc_mut.int_val;
    }

    // Check filter to determine image format
    ConstItem filter_item = image_dict->get("Filter");
    const char* filter = nullptr;
    if (filter_item.item != ITEM_NULL && filter_item.type_id() == LMD_TYPE_STRING) {
        String* filter_str = filter_item.string();
        if (filter_str) filter = filter_str->chars;
    }

    log_debug("Image '%s': filter=%s, bpc=%d", name, filter ? filter : "none", bits_per_component);

    // Get stream data as binary (Binary is typedef of String in Lambda)
    String* stream_data = nullptr;
    if (stream_data_item.type_id() == LMD_TYPE_BINARY || stream_data_item.type_id() == LMD_TYPE_STRING) {
        stream_data = stream_data_item.string();
    }

    if (!stream_data || stream_data->len == 0) {
        log_warn("Image '%s' has empty stream data", name);
        return;
    }

    // Create image view at current CTM position
    // The CTM contains the image placement and scaling:
    // [sx 0 0 sy tx ty] where sx, sy are width/height in user units
    double ctm_width = parser->state.ctm[0];   // x-scale (image width in user units)
    double ctm_height = parser->state.ctm[3];  // y-scale (image height in user units)
    double ctm_x = parser->state.ctm[4];       // x-position
    double ctm_y = parser->state.ctm[5];       // y-position

    // For PDF, y increases upward but we render with y increasing downward
    // Also, the CTM y-scale is typically positive for upward-increasing
    // We need to flip the image position

    log_debug("Image CTM: width=%.2f, height=%.2f, x=%.2f, y=%.2f",
              ctm_width, ctm_height, ctm_x, ctm_y);

    // Check if this is a pass-through format (DCT/JPEG or JPX/JPEG2000)
    bool is_jpeg = filter && (strcmp(filter, "DCTDecode") == 0);
    bool is_jpeg2k = filter && (strcmp(filter, "JPXDecode") == 0);

    ImageSurface* img_surface = nullptr;

    if (is_jpeg || is_jpeg2k) {
        // Decode JPEG/JPEG2000 using platform decoder
        img_surface = decode_image_data((const uint8_t*)stream_data->chars, stream_data->len,
                                         is_jpeg ? IMAGE_FORMAT_JPEG : IMAGE_FORMAT_UNKNOWN);
    } else {
        // Raw image data - need to convert to RGBA
        img_surface = decode_raw_image_data(image_dict, stream_data, img_width, img_height,
                                             bits_per_component, input->pool);
    }

    if (!img_surface) {
        log_warn("Failed to decode image '%s'", name);
        return;
    }

    // Create a ViewBlock for the image
    ViewBlock* img_view = (ViewBlock*)pool_calloc(view_pool, sizeof(ViewBlock));
    if (!img_view) return;

    img_view->view_type = RDT_VIEW_BLOCK;
    img_view->node_type = DOM_NODE_ELEMENT;
    img_view->tag_id = HTM_TAG_IMG;

    // Allocate EmbedProp to hold the image
    EmbedProp* embed = (EmbedProp*)pool_calloc(view_pool, sizeof(EmbedProp));
    embed->img = img_surface;
    img_view->embed = embed;

    // Set position based on CTM (convert PDF coordinates to screen)
    // PDF origin is bottom-left, we need top-left
    // Use absolute positioning
    img_view->x = (float)ctm_x;
    img_view->y = (float)ctm_y;  // Will be flipped by page height later
    img_view->width = (float)fabs(ctm_width);
    img_view->height = (float)fabs(ctm_height);

    log_debug("Created image view: pos=(%.2f, %.2f), size=(%.2f, %.2f)",
              img_view->x, img_view->y, img_view->width, img_view->height);

    append_child_view((View*)parent, (View*)img_view);
}

/**
 * Handle Form XObject - process nested content stream
 */
static void handle_form_xobject(Input* input, Pool* view_pool, ViewBlock* parent,
                                 PDFStreamParser* parser, Map* form_dict, const char* name) {
    // Form XObjects are essentially embedded content streams with their own resources
    // They have: /BBox, /Matrix (optional), /Resources (optional), stream content

    // Get form resources (may inherit from parent)
    Map* form_resources = nullptr;
    ConstItem res_item = form_dict->get("Resources");
    if (res_item.item != ITEM_NULL && res_item.type_id() == LMD_TYPE_MAP) {
        form_resources = (Map*)res_item.map;
    } else {
        // Inherit parent resources
        form_resources = (Map*)parser->resources;
    }

    // Get stream data
    ConstItem stream_data_item = form_dict->get("stream_data");
    if (stream_data_item.item == ITEM_NULL) {
        log_warn("Form XObject '%s' has no stream_data", name);
        return;
    }

    log_debug("Processing Form XObject '%s'", name);

    // TODO: Apply form matrix and save/restore graphics state
    // For now, just process the form content stream recursively
    process_pdf_stream(input, view_pool, parent, form_dict, form_resources, parser->pdf_data);
}

/**
 * Decode raw image data to RGBA ImageSurface
 * Handles various color spaces and bit depths
 */
static ImageSurface* decode_raw_image_data(Map* image_dict, String* data,
                                            int width, int height, int bpc, Pool* pool) {
    // Get color space
    ConstItem cs_item = image_dict->get("ColorSpace");
    PDFColorSpaceType cs_type = PDF_CS_DEVICE_RGB;  // Default
    int components = 3;  // Default RGB

    // Variables for indexed color space
    uint8_t* indexed_lookup = nullptr;
    int indexed_hival = 0;
    PDFColorSpaceType indexed_base = PDF_CS_DEVICE_RGB;
    int indexed_base_components = 3;

    if (cs_item.item != ITEM_NULL) {
        TypeId item_type = cs_item.type_id();

        if (item_type == LMD_TYPE_STRING || item_type == LMD_TYPE_SYMBOL) {
            // Simple named color space
            String* cs_str = cs_item.string();
            if (cs_str) {
                if (strcmp(cs_str->chars, "DeviceGray") == 0 || strcmp(cs_str->chars, "G") == 0) {
                    cs_type = PDF_CS_DEVICE_GRAY;
                    components = 1;
                } else if (strcmp(cs_str->chars, "DeviceCMYK") == 0 || strcmp(cs_str->chars, "CMYK") == 0) {
                    cs_type = PDF_CS_DEVICE_CMYK;
                    components = 4;
                } else if (strcmp(cs_str->chars, "DeviceRGB") == 0 || strcmp(cs_str->chars, "RGB") == 0) {
                    cs_type = PDF_CS_DEVICE_RGB;
                    components = 3;
                }
                log_debug("decode_raw_image: colorspace=%s, components=%d, bpc=%d", cs_str->chars, components, bpc);
            }
        } else if (item_type == LMD_TYPE_ARRAY) {
            // Array-based color space (Indexed, ICCBased, etc.)
            Array* cs_array = ((Item*)&cs_item)->array;
            if (cs_array && cs_array->length >= 1) {
                Item type_item = cs_array->items[0];
                String* type_name = type_item.get_string();

                if (type_name) {
                    log_debug("decode_raw_image: array colorspace type=%s", type_name->chars);

                    if (strcmp(type_name->chars, "Indexed") == 0 || strcmp(type_name->chars, "I") == 0) {
                        cs_type = PDF_CS_INDEXED;
                        components = 1;  // Indexed uses single index value

                        if (cs_array->length >= 4) {
                            // Parse base color space
                            Item base_item = cs_array->items[1];
                            String* base_name = base_item.get_string();
                            if (base_name) {
                                if (strcmp(base_name->chars, "DeviceGray") == 0) {
                                    indexed_base = PDF_CS_DEVICE_GRAY;
                                    indexed_base_components = 1;
                                } else if (strcmp(base_name->chars, "DeviceCMYK") == 0) {
                                    indexed_base = PDF_CS_DEVICE_CMYK;
                                    indexed_base_components = 4;
                                } else {
                                    indexed_base = PDF_CS_DEVICE_RGB;
                                    indexed_base_components = 3;
                                }
                            }

                            // Parse hival
                            Item hival_item = cs_array->items[2];
                            if (get_type_id(hival_item) == LMD_TYPE_INT) {
                                indexed_hival = hival_item.int_val;
                            } else if (get_type_id(hival_item) == LMD_TYPE_FLOAT) {
                                indexed_hival = (int)hival_item.get_double();
                            }

                            // Parse lookup table
                            Item lookup_item = cs_array->items[3];
                            TypeId lookup_type = get_type_id(lookup_item);
                            if (lookup_type == LMD_TYPE_STRING || lookup_type == LMD_TYPE_BINARY) {
                                String* lookup_str = lookup_item.get_string();
                                if (lookup_str) {
                                    indexed_lookup = (uint8_t*)lookup_str->chars;
                                    log_debug("Indexed image: hival=%d, base=%d, lookup_size=%d",
                                             indexed_hival, indexed_base, (int)lookup_str->len);
                                }
                            }
                        }
                    } else if (strcmp(type_name->chars, "ICCBased") == 0) {
                        cs_type = PDF_CS_ICCBASED;
                        // Try to get N from the stream dictionary
                        if (cs_array->length >= 2) {
                            Item stream_item = cs_array->items[1];
                            if (get_type_id(stream_item) == LMD_TYPE_MAP) {
                                Map* stream_dict = stream_item.map;
                                ConstItem n_item = stream_dict->get("N");
                                if (n_item.item != ITEM_NULL) {
                                    Item n_mut = *(Item*)&n_item;
                                    if (n_mut.type_id() == LMD_TYPE_INT) {
                                        components = n_mut.int_val;
                                    } else if (n_mut.type_id() == LMD_TYPE_FLOAT) {
                                        components = (int)n_mut.get_double();
                                    }
                                }
                            }
                        }
                        if (components <= 0) components = 3;  // Default to RGB
                        log_debug("ICCBased image: N=%d", components);
                    } else if (strcmp(type_name->chars, "CalGray") == 0) {
                        cs_type = PDF_CS_CAL_GRAY;
                        components = 1;
                    } else if (strcmp(type_name->chars, "CalRGB") == 0) {
                        cs_type = PDF_CS_CAL_RGB;
                        components = 3;
                    }
                }
            }
        }
    }

    // Calculate expected data size
    int row_bytes = (width * components * bpc + 7) / 8;
    int expected_size = row_bytes * height;

    if ((int)data->len < expected_size) {
        log_warn("Image data too short: got %d, expected %d", (int)data->len, expected_size);
        // Proceed anyway with available data
    }

    // Create RGBA surface
    ImageSurface* surface = image_surface_create(width, height);
    if (!surface) return nullptr;

    uint32_t* pixels = (uint32_t*)surface->pixels;
    const uint8_t* src = (const uint8_t*)data->chars;

    // Convert to RGBA based on color space and bit depth
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            if (cs_type == PDF_CS_INDEXED && indexed_lookup) {
                // Indexed color space - look up color in palette
                int idx = 0;
                if (bpc == 8) {
                    int src_idx = y * width + x;
                    if (src_idx < (int)data->len) {
                        idx = src[src_idx];
                    }
                } else if (bpc == 4) {
                    int nibble_idx = y * width + x;
                    int byte_idx = nibble_idx / 2;
                    bool high_nibble = (nibble_idx % 2) == 0;
                    if (byte_idx < (int)data->len) {
                        idx = high_nibble ? (src[byte_idx] >> 4) : (src[byte_idx] & 0x0F);
                    }
                } else if (bpc == 1) {
                    int bit_idx = y * width + x;
                    int byte_idx = bit_idx / 8;
                    int bit_offset = 7 - (bit_idx % 8);
                    if (byte_idx < (int)data->len) {
                        idx = (src[byte_idx] >> bit_offset) & 1;
                    }
                }

                if (idx > indexed_hival) idx = indexed_hival;
                int offset = idx * indexed_base_components;

                if (indexed_base == PDF_CS_DEVICE_RGB) {
                    r = indexed_lookup[offset];
                    g = indexed_lookup[offset + 1];
                    b = indexed_lookup[offset + 2];
                } else if (indexed_base == PDF_CS_DEVICE_GRAY) {
                    r = g = b = indexed_lookup[offset];
                } else if (indexed_base == PDF_CS_DEVICE_CMYK) {
                    float c = indexed_lookup[offset] / 255.0f;
                    float m = indexed_lookup[offset + 1] / 255.0f;
                    float yy = indexed_lookup[offset + 2] / 255.0f;
                    float k = indexed_lookup[offset + 3] / 255.0f;
                    r = (uint8_t)((1.0f - c) * (1.0f - k) * 255);
                    g = (uint8_t)((1.0f - m) * (1.0f - k) * 255);
                    b = (uint8_t)((1.0f - yy) * (1.0f - k) * 255);
                }
            } else if (bpc == 8) {
                if (components == 1) {
                    // Grayscale
                    int idx = y * width + x;
                    if (idx < (int)data->len) {
                        r = g = b = src[idx];
                    }
                } else if (components == 3) {
                    // RGB
                    int idx = (y * width + x) * 3;
                    if (idx + 2 < (int)data->len) {
                        r = src[idx];
                        g = src[idx + 1];
                        b = src[idx + 2];
                    }
                } else if (components == 4) {
                    // CMYK - convert to RGB
                    int idx = (y * width + x) * 4;
                    if (idx + 3 < (int)data->len) {
                        float c = src[idx] / 255.0f;
                        float m = src[idx + 1] / 255.0f;
                        float yy = src[idx + 2] / 255.0f;
                        float k = src[idx + 3] / 255.0f;
                        r = (uint8_t)((1.0f - c) * (1.0f - k) * 255);
                        g = (uint8_t)((1.0f - m) * (1.0f - k) * 255);
                        b = (uint8_t)((1.0f - yy) * (1.0f - k) * 255);
                    }
                }
            } else if (bpc == 1) {
                // 1-bit image (black and white)
                int bit_idx = y * width + x;
                int byte_idx = bit_idx / 8;
                int bit_offset = 7 - (bit_idx % 8);
                if (byte_idx < (int)data->len) {
                    uint8_t val = (src[byte_idx] >> bit_offset) & 1;
                    r = g = b = val ? 255 : 0;
                }
            } else if (bpc == 4) {
                // 4-bit grayscale
                int nibble_idx = y * width + x;
                int byte_idx = nibble_idx / 2;
                bool high_nibble = (nibble_idx % 2) == 0;
                if (byte_idx < (int)data->len) {
                    uint8_t val = high_nibble ? (src[byte_idx] >> 4) : (src[byte_idx] & 0x0F);
                    r = g = b = val * 17;  // Scale 0-15 to 0-255
                }
            }

            // Store as ABGR format (alpha in high byte)
            pixels[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }

    return surface;
}

// Include image library header
extern "C" {
    #include "../../lib/image.h"
}

/**
 * Decode JPEG/JPEG2000 image data using platform decoder
 */
static ImageSurface* decode_image_data(const uint8_t* data, size_t length, ImageFormat format_hint) {
    (void)format_hint; // Currently auto-detected

    int width, height, channels;
    unsigned char* pixels = image_load_from_memory(data, length, &width, &height, &channels);

    if (!pixels) {
        log_warn("Failed to decode image data (%zu bytes)", length);
        return nullptr;
    }

    // Create ImageSurface from decoded pixels
    ImageSurface* surface = image_surface_create_from(width, height, pixels);
    if (surface) {
        log_debug("Successfully decoded image: %dx%d", surface->width, surface->height);
    } else {
        image_free(pixels);
        log_warn("Failed to create image surface");
    }

    return surface;
}
