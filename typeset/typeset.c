#include "typeset.h"
#include <stdlib.h>
#include <string.h>

// TypesetEngine functions
TypesetEngine* typeset_engine_create(Context* ctx) {
    if (!ctx) return NULL;  // Require valid context
    
    TypesetEngine* engine = malloc(sizeof(TypesetEngine));
    if (!engine) return NULL;
    
    engine->lambda_context = ctx;
    engine->options = typeset_options_create_default();
    
    if (!engine->options) {
        typeset_engine_destroy(engine);
        return NULL;
    }
    
    // Initialize statistics
    engine->stats.documents_processed = 0;
    engine->stats.pages_generated = 0;
    engine->stats.total_layout_time = 0.0;
    engine->stats.memory_usage = 0;
    
    return engine;
}

void typeset_engine_destroy(TypesetEngine* engine) {
    if (!engine) return;
    
    if (engine->options) {
        typeset_options_destroy(engine->options);
    }
    
    free(engine);
}

// Main typesetting function - produces device-independent view tree
ViewTree* typeset_create_view_tree(TypesetEngine* engine, Item content, TypesetOptions* options) {
    if (!engine || !content) return NULL;
    
    // Use provided options or fallback to engine defaults
    TypesetOptions* opts = options ? options : engine->options;
    
    // Create root view node
    ViewNode* root = view_node_create(VIEW_NODE_DOCUMENT);
    if (!root) return NULL;
    
    // Create view tree with root
    ViewTree* tree = view_tree_create_with_root(root);
    if (!tree) {
        view_node_release(root);
        return NULL;
    }
    
    // Update statistics
    engine->stats.documents_processed++;
    
    return tree;
}

// Options management
TypesetOptions* typeset_options_create_default(void) {
    TypesetOptions* options = malloc(sizeof(TypesetOptions));
    if (!options) return NULL;
    
    // Set default values
    options->page_width = TYPESET_DEFAULT_PAGE_WIDTH;
    options->page_height = TYPESET_DEFAULT_PAGE_HEIGHT;
    options->margin_left = TYPESET_DEFAULT_MARGIN;
    options->margin_right = TYPESET_DEFAULT_MARGIN;
    options->margin_top = TYPESET_DEFAULT_MARGIN;
    options->margin_bottom = TYPESET_DEFAULT_MARGIN;
    
    options->default_font_family = strdup("Times");
    options->default_font_size = TYPESET_DEFAULT_FONT_SIZE;
    options->line_height = TYPESET_DEFAULT_LINE_HEIGHT;
    options->paragraph_spacing = 12.0;
    
    options->optimize_layout = true;
    options->show_debug_info = false;
    
    return options;
}

void typeset_options_destroy(TypesetOptions* options) {
    if (!options) return;
    
    free(options->default_font_family);
    free(options);
}

// Lambda function integration
Item fn_typeset(Context* ctx, Item* args, int arg_count) {
    if (!ctx || !args || arg_count < 1) return 0;
    
    // Create typeset engine
    TypesetEngine* engine = typeset_engine_create(ctx);
    if (!engine) return 0;
    
    // Get content from first argument
    Item content = args[0];
    
    // Create view tree
    ViewTree* tree = typeset_create_view_tree(engine, content, NULL);
    
    // For now, return success indicator (TODO: proper conversion)
    Item result = tree ? 1 : 0;
    
    // Release tree and cleanup
    if (tree) view_tree_release(tree);
    typeset_engine_destroy(engine);
    
    return result;
}
