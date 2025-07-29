#include "renderer.h"
#include "svg_renderer.h"
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
    } else {
        free(renderer);
        return NULL; // Only SVG supported for now
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
    }
    
    free(renderer);
}

bool view_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output, ViewRenderOptions* options) {
    if (!renderer || !tree || !output) return false;
    
    if (renderer->format == VIEW_FORMAT_SVG) {
        // Use the SVG renderer
        SVGRenderer* svg_renderer = (SVGRenderer*)renderer->renderer_data;
        return svg_render_view_tree(svg_renderer, tree, output);
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

ViewRenderOptions* view_render_options_create_default(void) {
    ViewRenderOptions* options = calloc(1, sizeof(ViewRenderOptions));
    if (!options) return NULL;
    
    options->format = VIEW_FORMAT_SVG;
    options->page_width = 612.0;
    options->page_height = 792.0;
    options->margin_left = 72.0;
    options->margin_top = 72.0;
    options->margin_right = 72.0;
    options->margin_bottom = 72.0;
    options->resolution = 300.0;
    options->compress_output = false;
    options->embed_fonts = false;
    options->optimize_paths = false;
    
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
