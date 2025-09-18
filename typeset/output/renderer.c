#include "renderer.h"
#include "svg_renderer.h"
#ifndef _WIN32
#include "pdf_renderer.h"
#endif
#include <stdlib.h>
#include <string.h>

// ViewRenderer implementation
ViewRenderer* view_renderer_create(const char* format_name) {
    ViewRenderer* renderer = calloc(1, sizeof(ViewRenderer));
    if (!renderer) return NULL;
    
    if (!format_name || strcmp(format_name, "svg") == 0) {
        renderer->format = VIEW_FORMAT_SVG;
        renderer->renderer_data = svg_renderer_create();
        if (!renderer->renderer_data) {
            free(renderer);
            return NULL;
        }
#ifndef _WIN32
    } else if (strcmp(format_name, "pdf") == 0) {
        renderer->format = VIEW_FORMAT_PDF;
        renderer->renderer_data = pdf_renderer_create(NULL); // Use default options
        if (!renderer->renderer_data) {
            free(renderer);
            return NULL;
        }
#endif
    } else {
        free(renderer);
        return NULL; // Unsupported format
    }
    
    return renderer;
}

ViewRenderer* view_renderer_create_default(void) {
    return view_renderer_create("svg");
}

void view_renderer_destroy(ViewRenderer* renderer) {
    if (!renderer) return;
    
    if (renderer->format == VIEW_FORMAT_SVG && renderer->renderer_data) {
        svg_renderer_destroy((SVGRenderer*)renderer->renderer_data);
#ifndef _WIN32
    } else if (renderer->format == VIEW_FORMAT_PDF && renderer->renderer_data) {
        pdf_renderer_destroy((PDFRenderer*)renderer->renderer_data);
#endif
    }
    
    free(renderer);
}

bool view_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output, ViewRenderOptions* options) {
    if (!renderer || !tree) return false;
    
    if (renderer->format == VIEW_FORMAT_SVG) {
        if (!output) return false;
        // Use the SVG renderer
        SVGRenderer* svg_renderer = (SVGRenderer*)renderer->renderer_data;
        return svg_render_view_tree(svg_renderer, tree, output);
#ifndef _WIN32
    } else if (renderer->format == VIEW_FORMAT_PDF) {
        // PDF doesn't use StrBuf output - it saves directly to file
        PDFRenderer* pdf_renderer = (PDFRenderer*)renderer->renderer_data;
        return pdf_render_view_tree(pdf_renderer, tree);
#endif
    }
    
    return false;
}

StrBuf* render_view_tree_to_svg(ViewTree* tree, ViewRenderOptions* options) {
    if (!tree) return NULL;
    
    ViewRenderer* renderer = view_renderer_create("svg");
    if (!renderer) return NULL;
    
    StrBuf* output = strbuf_new();
    if (!output) {
        view_renderer_destroy(renderer);
        return NULL;
    }
    
    bool success = view_render_tree(renderer, tree, output, options);
    
    view_renderer_destroy(renderer);
    
    if (!success) {
        strbuf_free(output);
        return NULL;
    }
    
    return output;
}

#ifndef _WIN32
bool render_view_tree_to_pdf_file(ViewTree* tree, const char* filename, ViewRenderOptions* options) {
    if (!tree || !filename) return false;
    
    ViewRenderer* renderer = view_renderer_create("pdf");
    if (!renderer) return false;
    
    // Render the tree (doesn't use StrBuf for PDF)
    bool success = view_render_tree(renderer, tree, NULL, options);
    
    if (success) {
        // Save to file
        PDFRenderer* pdf_renderer = (PDFRenderer*)renderer->renderer_data;
        success = pdf_save_to_file(pdf_renderer, filename);
    }
    
    view_renderer_destroy(renderer);
    return success;
}
#endif

ViewRenderOptions* view_render_options_create_default(void) {
    ViewRenderOptions* options = calloc(1, sizeof(ViewRenderOptions));
    if (!options) return NULL;
    
    options->format = VIEW_FORMAT_SVG;
    options->dpi = 72.0;
    options->embed_fonts = false;
    options->optimize_output = false;
    options->color_space = VIEW_COLOR_SPACE_RGB;
    options->quality = VIEW_RENDER_QUALITY_NORMAL;
    options->anti_alias = true;
    options->scale_factor = 1.0;
    options->include_metadata = true;
    options->include_accessibility = false;
    options->viewport = NULL;
    options->clip_to_viewport = false;
    
    return options;
}

void view_render_options_destroy(ViewRenderOptions* options) {
    if (options) {
        free(options);
    }
}

ViewRenderOptions* view_render_options_copy(ViewRenderOptions* options) {
    if (!options) return NULL;
    
    ViewRenderOptions* copy = malloc(sizeof(ViewRenderOptions));
    if (!copy) return NULL;
    
    *copy = *options;
    return copy;
}
