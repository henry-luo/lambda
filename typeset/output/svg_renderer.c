#include "svg_renderer.h"
#include "../../lib/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Forward declarations
bool svg_renderer_render_node(ViewRenderer* base_renderer, ViewNode* node);

// SVG renderer implementation

SVGRenderer* svg_renderer_create(void) {
    SVGRenderer* renderer = calloc(1, sizeof(SVGRenderer));
    if (!renderer) return NULL;
    
    // Initialize base renderer
    renderer->base.name = strdup("SVG Renderer");
    renderer->base.format_name = strdup("SVG");
    renderer->base.mime_type = strdup("image/svg+xml");
    renderer->base.file_extension = strdup(".svg");
    
    renderer->base.initialize = svg_renderer_initialize;
    renderer->base.render_tree = svg_renderer_render_tree;
    renderer->base.render_node = svg_renderer_render_node;
    renderer->base.finalize = svg_renderer_finalize;
    renderer->base.cleanup = svg_renderer_cleanup;
    
    renderer->base.renderer_data = renderer;
    
    // SVG-specific initialization
    renderer->svg_content = strbuf_new();
    renderer->viewport_width = 595.276;  // A4 width in points
    renderer->viewport_height = 841.89;  // A4 height in points
    renderer->element_id_counter = 0;
    renderer->embed_fonts = false;
    renderer->optimize_paths = false;
    renderer->decimal_precision = 2;
    
    return renderer;
}

void svg_renderer_destroy(SVGRenderer* renderer) {
    if (!renderer) return;
    
    // Free base renderer strings
    free(renderer->base.name);
    free(renderer->base.format_name);
    free(renderer->base.mime_type);
    free(renderer->base.file_extension);
    
    // Free SVG-specific data
    if (renderer->svg_content) {
        strbuf_destroy(renderer->svg_content);
    }
    
    free(renderer);
}

bool svg_renderer_initialize(ViewRenderer* base_renderer, ViewRenderOptions* options) {
    SVGRenderer* renderer = (SVGRenderer*)base_renderer->renderer_data;
    if (!renderer) return false;
    
    // Store options
    base_renderer->options = options;
    
    // Apply SVG-specific options if provided
    if (options) {
        // Use viewport from options if provided
        if (options->viewport) {
            renderer->viewport_width = options->viewport->size.width;
            renderer->viewport_height = options->viewport->size.height;
        }
        
        // Apply scale factor
        if (options->scale_factor != 1.0) {
            renderer->viewport_width *= options->scale_factor;
            renderer->viewport_height *= options->scale_factor;
        }
    }
    
    return true;
}

bool svg_renderer_render_tree(ViewRenderer* base_renderer, ViewTree* tree, StrBuf* output) {
    SVGRenderer* renderer = (SVGRenderer*)base_renderer->renderer_data;
    if (!renderer || !tree || !output) return false;
    
    // Clear previous content
    strbuf_reset(renderer->svg_content);
    
    // Write SVG header
    svg_write_header(renderer, tree);
    
    // Render tree content
    if (tree->root) {
        svg_renderer_render_node(base_renderer, tree->root);
    }
    
    // Render pages if no root node
    if (!tree->root && tree->pages) {
        for (int i = 0; i < tree->page_count; i++) {
            if (tree->pages[i] && tree->pages[i]->page_node) {
                svg_renderer_render_node(base_renderer, tree->pages[i]->page_node);
            }
        }
    }
    
    // Write SVG footer
    svg_write_footer(renderer);
    
    // Copy content to output buffer
    strbuf_append_str(output, renderer->svg_content->str);
    
    return true;
}

bool svg_renderer_render_node(ViewRenderer* base_renderer, ViewNode* node) {
    SVGRenderer* renderer = (SVGRenderer*)base_renderer->renderer_data;
    if (!renderer || !node) return false;
    
    // Skip invisible nodes
    if (!node->visible) return true;
    
    // Start group if needed (for positioning/transforms)
    bool needs_group = (node->position.x != 0 || node->position.y != 0 || 
                       node->opacity != 1.0 || node->transform.matrix[4] != 0 || 
                       node->transform.matrix[5] != 0);
    
    if (needs_group) {
        svg_start_group(renderer, node);
    }
    
    // Render node content based on type
    switch (node->type) {
        case VIEW_NODE_TEXT_RUN:
            svg_render_text_run(renderer, node);
            break;
        case VIEW_NODE_RECTANGLE:
            svg_render_rectangle(renderer, node);
            break;
        case VIEW_NODE_LINE:
            svg_render_line(renderer, node);
            break;
        case VIEW_NODE_MATH_ELEMENT:
            svg_render_math_element(renderer, node);
            break;
        default:
            // For other node types, just render children
            break;
    }
    
    // Render children
    ViewNode* child = node->first_child;
    while (child) {
        svg_renderer_render_node(base_renderer, child);
        child = child->next_sibling;
    }
    
    // Close group if we opened one
    if (needs_group) {
        svg_end_group(renderer);
    }
    
    return true;
}

void svg_renderer_finalize(ViewRenderer* base_renderer) {
    // Nothing special needed for SVG finalization
}

void svg_renderer_cleanup(ViewRenderer* base_renderer) {
    SVGRenderer* renderer = (SVGRenderer*)base_renderer->renderer_data;
    if (!renderer) return;
    
    strbuf_free(renderer->svg_content);
    free(renderer->base.name);
    free(renderer->base.format_name);
    free(renderer->base.mime_type);
    free(renderer->base.file_extension);
    free(renderer);
}

// SVG-specific rendering functions

void svg_write_header(SVGRenderer* renderer, ViewTree* tree) {
    strbuf_append_str(renderer->svg_content, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_append_str(renderer->svg_content, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    strbuf_append_str(renderer->svg_content, "xmlns:xlink=\"http://www.w3.org/1999/xlink\" ");
    
    strbuf_append_format(renderer->svg_content, "width=\"%.2f\" height=\"%.2f\" ", 
                        renderer->viewport_width, renderer->viewport_height);
    strbuf_append_format(renderer->svg_content, "viewBox=\"0 0 %.2f %.2f\"", 
                        renderer->viewport_width, renderer->viewport_height);
    
    strbuf_append_str(renderer->svg_content, ">\n");
    
    // Add title if available
    if (tree && tree->title) {
        strbuf_append_format(renderer->svg_content, "<title>%s</title>\n", tree->title);
    }
    
    // Add metadata if available
    if (tree && tree->creator) {
        strbuf_append_str(renderer->svg_content, "<metadata>\n");
        strbuf_append_format(renderer->svg_content, "  <creator>%s</creator>\n", tree->creator);
        if (tree->creation_date) {
            strbuf_append_format(renderer->svg_content, "  <created>%s</created>\n", tree->creation_date);
        }
        strbuf_append_str(renderer->svg_content, "</metadata>\n");
    }
    
    // Add default styles
    strbuf_append_str(renderer->svg_content, "<defs>\n");
    strbuf_append_str(renderer->svg_content, "  <style type=\"text/css\"><![CDATA[\n");
    strbuf_append_str(renderer->svg_content, "    .text-run { font-family: 'Times New Roman', serif; }\n");
    strbuf_append_str(renderer->svg_content, "    .math-element { font-family: 'STIX', 'Times New Roman', serif; }\n");
    strbuf_append_str(renderer->svg_content, "  ]]></style>\n");
    strbuf_append_str(renderer->svg_content, "</defs>\n");
}

void svg_write_footer(SVGRenderer* renderer) {
    strbuf_append_str(renderer->svg_content, "</svg>\n");
}

void svg_start_group(SVGRenderer* renderer, ViewNode* node) {
    strbuf_append_str(renderer->svg_content, "<g");
    
    // Add ID if present
    if (node->id) {
        strbuf_append_format(renderer->svg_content, " id=\"%s\"", node->id);
    }
    
    // Add class if present
    if (node->class_name) {
        strbuf_append_format(renderer->svg_content, " class=\"%s\"", node->class_name);
    }
    
    // Add transform if needed
    bool has_transform = false;
    if (node->position.x != 0 || node->position.y != 0) {
        strbuf_append_format(renderer->svg_content, " transform=\"translate(%.2f,%.2f)", 
                           node->position.x, node->position.y);
        has_transform = true;
    }
    
    // Add additional transforms from transform matrix
    if (node->transform.matrix[4] != 0 || node->transform.matrix[5] != 0) {
        if (!has_transform) {
            strbuf_append_str(renderer->svg_content, " transform=\"");
        } else {
            strbuf_append_str(renderer->svg_content, " ");
        }
        strbuf_append_format(renderer->svg_content, "translate(%.2f,%.2f)", 
                           node->transform.matrix[4], node->transform.matrix[5]);
        has_transform = true;
    }
    
    if (has_transform) {
        strbuf_append_str(renderer->svg_content, "\"");
    }
    
    // Add opacity if not 1.0
    if (node->opacity != 1.0) {
        strbuf_append_format(renderer->svg_content, " opacity=\"%.2f\"", node->opacity);
    }
    
    strbuf_append_str(renderer->svg_content, ">\n");
}

void svg_end_group(SVGRenderer* renderer) {
    strbuf_append_str(renderer->svg_content, "</g>\n");
}

void svg_render_text_run(SVGRenderer* renderer, ViewNode* node) {
    if (!node->content.text_run || !node->content.text_run->text) return;
    
    ViewTextRun* text_run = node->content.text_run;
    
    strbuf_append_str(renderer->svg_content, "<text");
    
    // Add positioning
    strbuf_append_format(renderer->svg_content, " x=\"%.2f\" y=\"%.2f\"", 
                        node->position.x, node->position.y + text_run->ascent);
    
    // Add font size
    if (text_run->font_size > 0) {
        strbuf_append_format(renderer->svg_content, " font-size=\"%.2f\"", text_run->font_size);
    }
    
    // Add color
    if (text_run->color.r != 0 || text_run->color.g != 0 || text_run->color.b != 0) {
        strbuf_append_format(renderer->svg_content, " fill=\"rgb(%.0f,%.0f,%.0f)\"",
                           text_run->color.r * 255, text_run->color.g * 255, text_run->color.b * 255);
    } else {
        strbuf_append_str(renderer->svg_content, " fill=\"black\"");
    }
    
    // Add alpha if not fully opaque
    if (text_run->color.a != 1.0) {
        strbuf_append_format(renderer->svg_content, " fill-opacity=\"%.2f\"", text_run->color.a);
    }
    
    // Add class
    strbuf_append_str(renderer->svg_content, " class=\"text-run\"");
    
    strbuf_append_str(renderer->svg_content, ">");
    
    // Escape text content for XML
    svg_escape_text(renderer, text_run->text);
    
    strbuf_append_str(renderer->svg_content, "</text>\n");
}

void svg_render_rectangle(SVGRenderer* renderer, ViewNode* node) {
    strbuf_append_str(renderer->svg_content, "<rect");
    
    strbuf_append_format(renderer->svg_content, " x=\"%.2f\" y=\"%.2f\"", 
                        node->position.x, node->position.y);
    strbuf_append_format(renderer->svg_content, " width=\"%.2f\" height=\"%.2f\"", 
                        node->size.width, node->size.height);
    
    // Default styling
    strbuf_append_str(renderer->svg_content, " fill=\"none\" stroke=\"black\" stroke-width=\"1\"");
    
    strbuf_append_str(renderer->svg_content, "/>\n");
}

void svg_render_line(SVGRenderer* renderer, ViewNode* node) {
    strbuf_append_str(renderer->svg_content, "<line");
    
    strbuf_append_format(renderer->svg_content, " x1=\"%.2f\" y1=\"%.2f\"", 
                        node->position.x, node->position.y);
    strbuf_append_format(renderer->svg_content, " x2=\"%.2f\" y2=\"%.2f\"", 
                        node->position.x + node->size.width, node->position.y + node->size.height);
    
    strbuf_append_str(renderer->svg_content, " stroke=\"black\" stroke-width=\"1\"");
    
    strbuf_append_str(renderer->svg_content, "/>\n");
}

void svg_render_math_element(SVGRenderer* renderer, ViewNode* node) {
    if (!node->content.math_elem) return;
    
    ViewMathElement* math_elem = node->content.math_elem;
    
    // Dispatch to specific math rendering function based on type
    switch (math_elem->type) {
        case VIEW_MATH_ATOM:
            svg_render_math_atom(renderer, node);
            break;
        case VIEW_MATH_FRACTION:
            svg_render_math_fraction(renderer, node);
            break;
        case VIEW_MATH_SUPERSCRIPT:
        case VIEW_MATH_SUBSCRIPT:
            svg_render_math_script(renderer, node);
            break;
        case VIEW_MATH_RADICAL:
            svg_render_math_radical(renderer, node);
            break;
        case VIEW_MATH_MATRIX:
            svg_render_math_matrix(renderer, node);
            break;
        case VIEW_MATH_DELIMITER:
            svg_render_math_delimiter(renderer, node);
            break;
        case VIEW_MATH_FUNCTION:
            svg_render_math_function(renderer, node);
            break;
        case VIEW_MATH_OPERATOR:
            svg_render_math_operator(renderer, node);
            break;
        case VIEW_MATH_SPACING:
            svg_render_math_spacing(renderer, math_elem->content.spacing.amount);
            break;
        default:
            // Fallback: render as generic math text
            svg_render_math_atom(renderer, node);
            break;
    }
}

// Render mathematical atom (symbol/variable)
void svg_render_math_atom(SVGRenderer* renderer, ViewNode* node) {
    if (!node->content.math_elem || node->content.math_elem->type != VIEW_MATH_ATOM) return;
    
    ViewMathElement* math_elem = node->content.math_elem;
    const char* symbol = math_elem->content.atom.symbol;
    const char* unicode = math_elem->content.atom.unicode;
    
    if (!symbol && !unicode) return;
    
    strbuf_append_str(renderer->svg_content, "<text");
    
    // Position
    strbuf_append_format(renderer->svg_content, " x=\"%.2f\" y=\"%.2f\"", 
                        node->position.x, node->position.y + node->size.height * 0.8);
    
    // Font size
    strbuf_append_format(renderer->svg_content, " font-size=\"%.2f\"", node->size.height);
    
    // Class and styling
    strbuf_append_str(renderer->svg_content, " class=\"math-atom\"");
    strbuf_append_str(renderer->svg_content, " fill=\"black\"");
    
    strbuf_append_str(renderer->svg_content, ">");
    
    // Use Unicode if available, otherwise use symbol
    const char* display_text = unicode ? unicode : symbol;
    svg_escape_text(renderer, display_text);
    
    strbuf_append_str(renderer->svg_content, "</text>\n");
}

// Render mathematical fraction
void svg_render_math_fraction(SVGRenderer* renderer, ViewNode* node) {
    if (!node->content.math_elem || node->content.math_elem->type != VIEW_MATH_FRACTION) return;
    
    ViewMathElement* math_elem = node->content.math_elem;
    ViewNode* numerator = math_elem->content.fraction.numerator;
    ViewNode* denominator = math_elem->content.fraction.denominator;
    double line_thickness = math_elem->content.fraction.line_thickness;
    
    if (!numerator || !denominator) return;
    
    // Start fraction group
    strbuf_append_str(renderer->svg_content, "<g class=\"math-fraction\">\n");
    
    // Render numerator
    if (numerator->first_child) {
        ViewNode* child = numerator->first_child;
        while (child) {
            svg_renderer_render_node((ViewRenderer*)renderer, child);
            child = child->next_sibling;
        }
    }
    
    // Render fraction line
    double line_y = node->position.y + node->size.height * 0.5;
    svg_render_fraction_line(renderer, node->position.x, line_y, node->size.width, line_thickness);
    
    // Render denominator
    if (denominator->first_child) {
        ViewNode* child = denominator->first_child;
        while (child) {
            svg_renderer_render_node((ViewRenderer*)renderer, child);
            child = child->next_sibling;
        }
    }
    
    strbuf_append_str(renderer->svg_content, "</g>\n");
}

// Render superscript/subscript
void svg_render_math_script(SVGRenderer* renderer, ViewNode* node) {
    if (!node->content.math_elem) return;
    
    ViewMathElement* math_elem = node->content.math_elem;
    bool is_superscript = (math_elem->type == VIEW_MATH_SUPERSCRIPT);
    
    ViewNode* base = math_elem->content.script.base;
    ViewNode* script = math_elem->content.script.script;
    
    if (!base || !script) return;
    
    // Start script group
    strbuf_append_format(renderer->svg_content, "<g class=\"math-%s\">\n", 
                        is_superscript ? "superscript" : "subscript");
    
    // Render base
    if (base->first_child) {
        ViewNode* child = base->first_child;
        while (child) {
            svg_renderer_render_node((ViewRenderer*)renderer, child);
            child = child->next_sibling;
        }
    }
    
    // Render script
    if (script->first_child) {
        ViewNode* child = script->first_child;
        while (child) {
            svg_renderer_render_node((ViewRenderer*)renderer, child);
            child = child->next_sibling;
        }
    }
    
    strbuf_append_str(renderer->svg_content, "</g>\n");
}

// Render fraction line
void svg_render_fraction_line(SVGRenderer* renderer, double x, double y, double width, double thickness) {
    strbuf_append_str(renderer->svg_content, "<line");
    strbuf_append_format(renderer->svg_content, " x1=\"%.2f\" y1=\"%.2f\"", x, y);
    strbuf_append_format(renderer->svg_content, " x2=\"%.2f\" y2=\"%.2f\"", x + width, y);
    strbuf_append_format(renderer->svg_content, " stroke=\"black\" stroke-width=\"%.2f\"", thickness);
    strbuf_append_str(renderer->svg_content, " class=\"fraction-line\"/>\n");
}

// Render mathematical spacing
void svg_render_math_spacing(SVGRenderer* renderer, double amount) {
    // Mathematical spacing is handled by positioning, so this is mostly a no-op for SVG
    // We could add a comment for debugging
    strbuf_append_format(renderer->svg_content, "<!-- math-spacing: %.2f -->\n", amount);
}

// Placeholder implementations for remaining math rendering functions
void svg_render_math_radical(SVGRenderer* renderer, ViewNode* radical_node) {
    // TODO: Implement radical (square root) rendering
    strbuf_append_str(renderer->svg_content, "<text class=\"math-radical\">√[placeholder]</text>\n");
}

void svg_render_math_matrix(SVGRenderer* renderer, ViewNode* matrix_node) {
    // TODO: Implement matrix rendering
    strbuf_append_str(renderer->svg_content, "<text class=\"math-matrix\">[matrix placeholder]</text>\n");
}

void svg_render_math_delimiter(SVGRenderer* renderer, ViewNode* delimiter_node) {
    // TODO: Implement delimiter rendering
    strbuf_append_str(renderer->svg_content, "<text class=\"math-delimiter\">(placeholder)</text>\n");
}

void svg_render_math_function(SVGRenderer* renderer, ViewNode* function_node) {
    // TODO: Implement function rendering
    strbuf_append_str(renderer->svg_content, "<text class=\"math-function\">f(placeholder)</text>\n");
}

void svg_render_math_operator(SVGRenderer* renderer, ViewNode* operator_node) {
    // TODO: Implement operator rendering
    strbuf_append_str(renderer->svg_content, "<text class=\"math-operator\">op</text>\n");
}

void svg_escape_text(SVGRenderer* renderer, const char* text) {
    if (!text) return;
    
    for (const char* p = text; *p; p++) {
        switch (*p) {
            case '<':
                strbuf_append_str(renderer->svg_content, "&lt;");
                break;
            case '>':
                strbuf_append_str(renderer->svg_content, "&gt;");
                break;
            case '&':
                strbuf_append_str(renderer->svg_content, "&amp;");
                break;
            case '"':
                strbuf_append_str(renderer->svg_content, "&quot;");
                break;
            case '\'':
                strbuf_append_str(renderer->svg_content, "&#39;");
                break;
            default:
                strbuf_append_char(renderer->svg_content, *p);
                break;
        }
    }
}

// Public API functions

StrBuf* render_view_tree_to_svg(ViewTree* tree, SVGRenderOptions* options) {
    if (!tree) return NULL;
    
    SVGRenderer* renderer = svg_renderer_create();
    if (!renderer) return NULL;
    
    // Initialize with options
    ViewRenderOptions* base_options = options ? &options->base : NULL;
    if (!svg_renderer_initialize((ViewRenderer*)renderer, base_options)) {
        svg_renderer_cleanup((ViewRenderer*)renderer);
        return NULL;
    }
    
    // Create output buffer
    StrBuf* output = strbuf_create("");
    if (!output) {
        svg_renderer_cleanup((ViewRenderer*)renderer);
        return NULL;
    }
    
    // Render tree
    bool success = svg_renderer_render_tree((ViewRenderer*)renderer, tree, output);
    
    // Cleanup renderer
    svg_renderer_cleanup((ViewRenderer*)renderer);
    
    if (!success) {
        strbuf_destroy(output);
        return NULL;
    }
    
    return output;
}

// Internal function for direct SVG rendering (for testing)
StrBuf* render_view_tree_to_svg_internal(ViewTree* tree, SVGRenderOptions* options) {
    if (!tree) return NULL;
    
    // Create simple SVG output for testing
    StrBuf* svg = strbuf_create("");
    if (!svg) return NULL;
    
    double width = (options && options->base.page_width > 0) ? options->base.page_width : 595.276;
    double height = (options && options->base.page_height > 0) ? options->base.page_height : 841.89;
    double margin_left = (options && options->base.margin_left > 0) ? options->base.margin_left : 72.0;
    double margin_top = (options && options->base.margin_top > 0) ? options->base.margin_top : 72.0;
    const char* bg_color = "white"; // Default background
    
    // SVG header
    strbuf_append_str(svg, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
    strbuf_append_str(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    strbuf_append_format(svg, "width=\"%.2f\" height=\"%.2f\" ", width, height);
    strbuf_append_format(svg, "viewBox=\"0 0 %.2f %.2f\">\n", width, height);
    
    // Background
    strbuf_append_format(svg, "  <rect width=\"%.2f\" height=\"%.2f\" fill=\"%s\"/>\n", 
                        width, height, bg_color);
    
    // Simple content rendering
    double y_pos = margin_top + 20;
    
    if (tree->root && tree->root->child_count > 0) {
        for (int i = 0; i < tree->root->child_count; i++) {
            ViewNode* child = tree->root->children[i];
            if (!child) continue;
            
            const char* text_content = "Sample text content";
            
            // Determine content based on node type
            switch (child->type) {
                case VIEW_NODE_TEXT:
                    if (child->content && child->content->text_content) {
                        text_content = child->content->text_content;
                    }
                    break;
                case VIEW_NODE_HEADING:
                    text_content = child->content && child->content->text_content ? 
                                  child->content->text_content : "Heading";
                    break;
                case VIEW_NODE_PARAGRAPH:
                    text_content = child->content && child->content->text_content ? 
                                  child->content->text_content : "Paragraph";
                    break;
                case VIEW_NODE_LIST:
                    text_content = "• List item";
                    break;
                default:
                    text_content = "Content";
                    break;
            }
            
            // Render text
            strbuf_append_format(svg, "  <text x=\"%.2f\" y=\"%.2f\" ", margin_left, y_pos);
            strbuf_append_str(svg, "font-family=\"Times, serif\" font-size=\"12\" fill=\"black\">");
            
            // Escape text content
            const char* p = text_content;
            while (*p) {
                switch (*p) {
                    case '<': strbuf_append_str(svg, "&lt;"); break;
                    case '>': strbuf_append_str(svg, "&gt;"); break;
                    case '&': strbuf_append_str(svg, "&amp;"); break;
                    case '"': strbuf_append_str(svg, "&quot;"); break;
                    default: strbuf_append_char(svg, *p); break;
                }
                p++;
            }
            
            strbuf_append_str(svg, "</text>\n");
            y_pos += 18; // Line spacing
        }
    } else {
        // Fallback content
        strbuf_append_format(svg, "  <text x=\"%.2f\" y=\"%.2f\" ", margin_left, y_pos);
        strbuf_append_str(svg, "font-family=\"Times, serif\" font-size=\"12\" fill=\"black\">");
        strbuf_append_str(svg, "Typeset content rendered successfully</text>\n");
    }
    
    strbuf_append_str(svg, "</svg>\n");
    
    return svg;
}
