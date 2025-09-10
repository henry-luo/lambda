#include "document_typeset.h"
#include "../view/view_tree.h"
#include "../layout/math_layout.h"
#include "../output/svg_renderer.h"
#include "../integration/lambda_math_bridge_new.h"
#include "../../lambda/format/format.h"
#include "../../lib/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Implementation of markdown + math document typesetting

// Create default document options
DocumentTypesetOptions* create_default_document_options(void) {
    DocumentTypesetOptions* options = calloc(1, sizeof(DocumentTypesetOptions));
    if (!options) return NULL;
    
    // Initialize base typeset options
    options->base_options.page_width = TYPESET_DEFAULT_PAGE_WIDTH;
    options->base_options.page_height = TYPESET_DEFAULT_PAGE_HEIGHT;
    options->base_options.margin_left = TYPESET_DEFAULT_MARGIN;
    options->base_options.margin_right = TYPESET_DEFAULT_MARGIN;
    options->base_options.margin_top = TYPESET_DEFAULT_MARGIN;
    options->base_options.margin_bottom = TYPESET_DEFAULT_MARGIN;
    options->base_options.default_font_family = strdup("Times New Roman");
    options->base_options.default_font_size = 12.0;
    options->base_options.line_height = 1.2;
    options->base_options.paragraph_spacing = 6.0;
    
    // Initialize math options
    options->math_options.font_size = 12.0;
    options->math_options.display_style = true;
    options->math_options.error_on_unknown_symbol = false;
    
    // Initialize document options
    options->render_math_as_svg = true;
    options->inline_math_baseline_align = true;
    options->math_scale_factor = 1.0;
    options->document_title = strdup("Mathematical Document");
    options->document_author = strdup("Lambda Typesetter");
    options->include_table_of_contents = false;
    options->number_sections = true;
    options->output_format = strdup("svg");
    options->standalone_output = true;
    
    return options;
}

// Destroy document options
void destroy_document_options(DocumentTypesetOptions* options) {
    if (!options) return;
    
    if (options->base_options.default_font_family) free(options->base_options.default_font_family);
    if (options->document_title) free(options->document_title);
    if (options->document_author) free(options->document_author);
    if (options->output_format) free(options->output_format);
    
    free(options);
}

// Destroy document result
void destroy_document_result(DocumentTypesetResult* result) {
    if (!result) return;
    
    if (result->view_tree) view_tree_destroy(result->view_tree);
    if (result->rendered_output) strbuf_destroy(result->rendered_output);
    if (result->error_message) free(result->error_message);
    
    free(result);
}

// Extract math expressions from markdown element tree
int extract_math_expressions(Element* lambda_element, Element** math_elements, int max_elements) {
    if (!lambda_element || !math_elements) return 0;
    
    int count = 0;
    List* element_list = (List*)lambda_element;
    
    // Check if this element is a math element
    TypeElmt* elem_type = (TypeElmt*)lambda_element->type;
    if (elem_type && elem_type->name.str && strcmp(elem_type->name.str, "math") == 0) {
        if (count < max_elements) {
            math_elements[count++] = lambda_element;
        }
        return count;
    }
    
    // Recursively search children
    for (long i = 0; i < element_list->length && count < max_elements; i++) {
        Item child_item = element_list->items[i];
        if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            Element* child_element = (Element*)child_item.pointer;
            count += extract_math_expressions(child_element, 
                                            &math_elements[count], 
                                            max_elements - count);
        }
    }
    
    return count;
}

// Process math element in document context
ViewNode* process_math_in_document(Element* math_element, const char* context, 
                                  DocumentTypesetOptions* options) {
    if (!math_element || !context) return NULL;
    
    log_info("Processing math element in context: %s", context);
    
    // Convert Lambda math element to ViewTree using our bridge
    ViewTree* math_tree = convert_lambda_math_to_viewtree(math_element);
    if (!math_tree || !math_tree->root) {
        log_error("Failed to convert math element to view tree");
        return NULL;
    }
    
    ViewNode* math_node = math_tree->root;
    
    // Apply context-specific styling
    if (strcmp(context, "inline") == 0) {
        // Inline math styling
        math_node->style.font_size = options->base_options.default_font_size * 0.9;
        math_node->style.baseline_offset = 0.0;  // Align with text baseline
        log_info("Applied inline math styling");
    } else if (strcmp(context, "display") == 0) {
        // Display math styling
        math_node->style.font_size = options->base_options.default_font_size * 1.1;
        math_node->style.margin_top = 12.0;
        math_node->style.margin_bottom = 12.0;
        log_info("Applied display math styling");
    }
    
    // Scale math according to options
    if (options->math_scale_factor != 1.0) {
        math_node->style.font_size *= options->math_scale_factor;
    }
    
    // Detach from original tree and return standalone node
    math_tree->root = NULL;
    view_tree_destroy(math_tree);
    
    return math_node;
}

// Process document structure and create hierarchical view tree
bool process_document_structure(Element* lambda_element, ViewTree* view_tree,
                               DocumentTypesetOptions* options) {
    if (!lambda_element || !view_tree) return false;
    
    log_info("Processing document structure");
    
    // Create document root node
    ViewNode* document_root = view_node_create(VIEW_NODE_CONTAINER);
    if (!document_root) return false;
    
    document_root->style.width = options->base_options.page_width - 
                                options->base_options.margin_left - 
                                options->base_options.margin_right;
    document_root->style.height = 0; // Will be calculated based on content
    
    // Process document elements
    List* element_list = (List*)lambda_element;
    ViewNode* current_page = NULL;
    double current_y = options->base_options.margin_top;
    
    for (long i = 0; i < element_list->length; i++) {
        Item child_item = element_list->items[i];
        if (get_type_id(child_item) != LMD_TYPE_ELEMENT) continue;
        
        Element* child_element = (Element*)child_item.pointer;
        TypeElmt* child_type = (TypeElmt*)child_element->type;
        if (!child_type || !child_type->name.str) continue;
        
        const char* element_type = child_type->name.str;
        log_info("Processing element: %s", element_type);
        
        ViewNode* element_node = NULL;
        
        if (strcmp(element_type, "h1") == 0 || strcmp(element_type, "h2") == 0 || 
            strcmp(element_type, "h3") == 0) {
            // Process heading
            element_node = process_heading_element(child_element, element_type, options);
        } else if (strcmp(element_type, "p") == 0) {
            // Process paragraph (may contain inline math)
            element_node = process_paragraph_element(child_element, options);
        } else if (strcmp(element_type, "math") == 0) {
            // Process standalone math
            String* type_attr = get_attribute(child_element, "type");
            const char* math_context = (type_attr && strcmp(type_attr->chars, "display") == 0) ? 
                                     "display" : "inline";
            element_node = process_math_in_document(child_element, math_context, options);
        } else {
            // Process other elements
            element_node = process_generic_element(child_element, options);
        }
        
        if (element_node) {
            // Position element and add to document
            element_node->position.x = options->base_options.margin_left;
            element_node->position.y = current_y;
            
            view_node_add_child(document_root, element_node);
            current_y += element_node->size.height + element_node->style.margin_bottom;
        }
    }
    
    // Set final document size
    document_root->size.height = current_y + options->base_options.margin_bottom;
    view_tree->document_size.width = options->base_options.page_width;
    view_tree->document_size.height = document_root->size.height;
    
    view_tree->root = document_root;
    
    log_info("Document structure processing completed");
    return true;
}

// Process paragraph element (may contain inline math)
ViewNode* process_paragraph_element(Element* paragraph_element, DocumentTypesetOptions* options) {
    if (!paragraph_element) return NULL;
    
    ViewNode* paragraph_node = view_node_create(VIEW_NODE_CONTAINER);
    if (!paragraph_node) return NULL;
    
    paragraph_node->style.font_size = options->base_options.default_font_size;
    paragraph_node->style.line_height = options->base_options.line_height;
    paragraph_node->style.margin_bottom = options->base_options.paragraph_spacing;
    
    List* element_list = (List*)paragraph_element;
    double current_x = 0.0;
    double line_height = options->base_options.default_font_size * options->base_options.line_height;
    
    for (long i = 0; i < element_list->length; i++) {
        Item child_item = element_list->items[i];
        ViewNode* child_node = NULL;
        
        if (get_type_id(child_item) == LMD_TYPE_STRING) {
            // Text content
            String* text = (String*)child_item.pointer;
            child_node = create_text_node(text, options);
        } else if (get_type_id(child_item) == LMD_TYPE_ELEMENT) {
            // Element content (possibly math)
            Element* child_element = (Element*)child_item.pointer;
            TypeElmt* child_type = (TypeElmt*)child_element->type;
            
            if (child_type && child_type->name.str && strcmp(child_type->name.str, "math") == 0) {
                child_node = process_math_in_document(child_element, "inline", options);
            } else {
                child_node = process_generic_element(child_element, options);
            }
        }
        
        if (child_node) {
            child_node->position.x = current_x;
            child_node->position.y = 0;
            view_node_add_child(paragraph_node, child_node);
            current_x += child_node->size.width;
        }
    }
    
    paragraph_node->size.width = current_x;
    paragraph_node->size.height = line_height;
    
    return paragraph_node;
}

// Process heading element
ViewNode* process_heading_element(Element* heading_element, const char* heading_type,
                                 DocumentTypesetOptions* options) {
    if (!heading_element || !heading_type) return NULL;
    
    ViewNode* heading_node = view_node_create(VIEW_NODE_TEXT);
    if (!heading_node) return NULL;
    
    // Set heading font size based on level
    double font_size = options->base_options.default_font_size;
    if (strcmp(heading_type, "h1") == 0) {
        font_size *= 2.0;
    } else if (strcmp(heading_type, "h2") == 0) {
        font_size *= 1.5;
    } else if (strcmp(heading_type, "h3") == 0) {
        font_size *= 1.2;
    }
    
    heading_node->style.font_size = font_size;
    heading_node->style.font_weight = 700; // Bold
    heading_node->style.margin_top = font_size * 0.5;
    heading_node->style.margin_bottom = font_size * 0.3;
    
    // Extract text content
    List* element_list = (List*)heading_element;
    if (element_list->length > 0) {
        Item first_item = element_list->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* heading_text = (String*)first_item.pointer;
            heading_node->content.text.text = strdup(heading_text->chars);
            heading_node->size.width = strlen(heading_text->chars) * font_size * 0.6; // Rough estimate
            heading_node->size.height = font_size * 1.2;
        }
    }
    
    return heading_node;
}

// Create text node from string
ViewNode* create_text_node(String* text, DocumentTypesetOptions* options) {
    if (!text) return NULL;
    
    ViewNode* text_node = view_node_create(VIEW_NODE_TEXT);
    if (!text_node) return NULL;
    
    text_node->content.text.text = strndup(text->chars, text->len);
    text_node->style.font_size = options->base_options.default_font_size;
    
    // Rough size estimation
    text_node->size.width = text->len * options->base_options.default_font_size * 0.6;
    text_node->size.height = options->base_options.default_font_size * options->base_options.line_height;
    
    return text_node;
}

// Process generic element
ViewNode* process_generic_element(Element* element, DocumentTypesetOptions* options) {
    if (!element) return NULL;
    
    ViewNode* generic_node = view_node_create(VIEW_NODE_CONTAINER);
    if (!generic_node) return NULL;
    
    generic_node->size.width = 100.0;  // Default size
    generic_node->size.height = 20.0;
    
    return generic_node;
}

// Main document typesetting function
DocumentTypesetResult* typeset_markdown_document(Element* lambda_element, 
                                                DocumentTypesetOptions* options) {
    if (!lambda_element) return NULL;
    
    clock_t start_time = clock();
    
    DocumentTypesetResult* result = calloc(1, sizeof(DocumentTypesetResult));
    if (!result) return NULL;
    
    log_info("Starting markdown document typesetting");
    
    // Create view tree
    result->view_tree = view_tree_create();
    if (!result->view_tree) {
        result->has_errors = true;
        result->error_message = strdup("Failed to create view tree");
        return result;
    }
    
    // Set document metadata
    result->view_tree->title = strdup(options->document_title);
    result->view_tree->creator = strdup("Lambda Document Typesetter");
    
    // Process document structure
    if (!process_document_structure(lambda_element, result->view_tree, options)) {
        result->has_errors = true;
        result->error_message = strdup("Failed to process document structure");
        return result;
    }
    
    // Extract and count math expressions
    Element* math_elements[100];
    int math_count = extract_math_expressions(lambda_element, math_elements, 100);
    result->math_expressions_count = math_count;
    
    // Count inline vs display math
    for (int i = 0; i < math_count; i++) {
        String* type_attr = get_attribute(math_elements[i], "type");
        if (type_attr && strcmp(type_attr->chars, "display") == 0) {
            result->display_math_count++;
        } else {
            result->inline_math_count++;
        }
    }
    
    // Generate output
    if (strcmp(options->output_format, "svg") == 0) {
        result->rendered_output = render_document_to_svg(result->view_tree, options);
        if (result->rendered_output) {
            result->output_size_bytes = result->rendered_output->length;
        }
    }
    
    // Calculate statistics
    clock_t end_time = clock();
    result->typeset_time_ms = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    result->total_pages = 1; // Simple single-page layout for now
    
    log_info("Document typesetting completed: %d math expressions, %.2f ms", 
             math_count, result->typeset_time_ms);
    
    return result;
}

// Render document to SVG
StrBuf* render_document_to_svg(ViewTree* document_tree, DocumentTypesetOptions* options) {
    if (!document_tree) return NULL;
    
    StrBuf* svg = strbuf_create(8192);
    if (!svg) return NULL;
    
    // SVG header
    strbuf_append_str(svg, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_append_str(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    strbuf_append_str(svg, "xmlns:xlink=\"http://www.w3.org/1999/xlink\" ");
    
    strbuf_append_format(svg, "width=\"%.1f\" height=\"%.1f\" ",
                        document_tree->document_size.width,
                        document_tree->document_size.height);
    
    strbuf_append_format(svg, "viewBox=\"0 0 %.1f %.1f\">\n",
                        document_tree->document_size.width,
                        document_tree->document_size.height);
    
    // Document title
    strbuf_append_str(svg, "  <title>");
    if (document_tree->title) {
        strbuf_append_str(svg, document_tree->title);
    } else {
        strbuf_append_str(svg, "Mathematical Document");
    }
    strbuf_append_str(svg, "</title>\n");
    
    // CSS styles
    strbuf_append_str(svg, "  <defs>\n");
    strbuf_append_str(svg, "    <style><![CDATA[\n");
    strbuf_append_str(svg, "      .document { font-family: 'Times New Roman', serif; }\n");
    strbuf_append_str(svg, "      .heading { font-weight: bold; }\n");
    strbuf_append_str(svg, "      .paragraph { font-size: 12px; }\n");
    strbuf_append_str(svg, "      .math-inline { font-family: 'Latin Modern Math', 'STIX', serif; }\n");
    strbuf_append_str(svg, "      .math-display { font-family: 'Latin Modern Math', 'STIX', serif; }\n");
    strbuf_append_str(svg, "      .math-fraction { text-anchor: middle; }\n");
    strbuf_append_str(svg, "    ]]></style>\n");
    strbuf_append_str(svg, "  </defs>\n");
    
    // Render document content
    strbuf_append_str(svg, "  <g class=\"document\">\n");
    
    if (document_tree->root) {
        render_view_node_to_svg(document_tree->root, svg, 0);
    }
    
    strbuf_append_str(svg, "  </g>\n");
    strbuf_append_str(svg, "</svg>\n");
    
    return svg;
}

// Render view node to SVG (recursive)
void render_view_node_to_svg(ViewNode* node, StrBuf* svg, int depth) {
    if (!node || !svg) return;
    
    const char* indent = "    ";
    for (int i = 0; i < depth; i++) {
        strbuf_append_str(svg, indent);
    }
    
    switch (node->type) {
        case VIEW_NODE_TEXT:
            strbuf_append_format(svg, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%.1f\" class=\"paragraph\">",
                               node->position.x, node->position.y + node->style.font_size,
                               node->style.font_size);
            if (node->content.text.text) {
                strbuf_append_str(svg, node->content.text.text);
            }
            strbuf_append_str(svg, "</text>\n");
            break;
            
        case VIEW_NODE_MATH_ELEMENT:
            strbuf_append_format(svg, "<g class=\"math-element\" transform=\"translate(%.1f,%.1f)\">\n",
                               node->position.x, node->position.y);
            render_math_element_to_svg(node, svg, depth + 1);
            for (int i = 0; i < depth; i++) strbuf_append_str(svg, indent);
            strbuf_append_str(svg, "</g>\n");
            break;
            
        case VIEW_NODE_CONTAINER:
            strbuf_append_format(svg, "<g class=\"container\" transform=\"translate(%.1f,%.1f)\">\n",
                               node->position.x, node->position.y);
            
            // Render children
            for (int i = 0; i < node->child_count; i++) {
                render_view_node_to_svg(node->children[i], svg, depth + 1);
            }
            
            for (int i = 0; i < depth; i++) strbuf_append_str(svg, indent);
            strbuf_append_str(svg, "</g>\n");
            break;
            
        default:
            strbuf_append_str(svg, "<!-- Unknown node type -->\n");
            break;
    }
}

// Render math element to SVG
void render_math_element_to_svg(ViewNode* math_node, StrBuf* svg, int depth) {
    if (!math_node || math_node->type != VIEW_NODE_MATH_ELEMENT) return;
    
    const char* indent = "    ";
    for (int i = 0; i < depth; i++) {
        strbuf_append_str(svg, indent);
    }
    
    // Simple math rendering - in real implementation this would use the full math renderer
    strbuf_append_str(svg, "<text class=\"math-inline\">Mathematical Expression</text>\n");
}

// Helper functions (forward declarations in implementation)
static String* get_attribute(Element* elem, const char* attr_name);

// Get attribute implementation (simplified version)
static String* get_attribute(Element* elem, const char* attr_name) {
    // This is a simplified implementation
    // Real implementation would access Lambda element attributes properly
    return NULL;
}
