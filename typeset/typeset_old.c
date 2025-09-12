#include "typeset.h"
#include <stdlib.h>
#include <string.h>

// Main typesetting engine implementation

TypesetEngine* typeset_engine_create(Context* ctx) {
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
    
    // Create a new view tree
    ViewTree* tree = view_tree_create();
    if (!tree) return NULL;
    
    // Create root view node
    ViewNode* root = view_node_create(VIEW_NODE_DOCUMENT);
    if (!root) {
        view_tree_destroy(tree);
        return NULL;
    }
    
    view_tree_set_root(tree, root);
    
    // Update statistics
    engine->stats.documents_processed++;
    
    return tree;
}
    doc->font_manager = engine->font_manager;
    
    // Create a simple text paragraph for the LaTeX content
    DocNode* paragraph = create_paragraph_node();
    DocNode* text = create_text_node(latex);
    docnode_append_child(paragraph, text);
    docnode_append_child(doc->root, paragraph);
    
    return doc;
}

Document* typeset_math_expression(TypesetEngine* engine, const char* math) {
    if (!engine || !math) return NULL;
    
    Document* doc = document_create(engine->lambda_context);
    if (!doc) return NULL;
    
    // Set up document with default settings
    doc->page_settings = page_settings_copy(engine->default_page_settings);
    doc->stylesheet = stylesheet_copy(engine->default_stylesheet);
    doc->font_manager = engine->font_manager;
    
    // Parse math expression and create a math node
    // This would use the existing input-math.c parser
    // For now, we'll create a placeholder math node
    DocNode* math_node = docnode_create(DOC_NODE_MATH_BLOCK);
    docnode_set_text_content(math_node, math);
    docnode_append_child(doc->root, math_node);
    
    return doc;
}

DocumentOutput* render_document_to_svg(TypesetEngine* engine, Document* doc) {
    if (!engine || !doc) return NULL;
    
    // Layout the document
    LayoutResult* layout_result = layout_document(doc);
    if (!layout_result || !layout_result->success) {
        if (layout_result) layout_result_destroy(layout_result);
        return NULL;
    }
    
    // Create document output
    DocumentOutput* output = document_output_create(doc);
    if (!output) {
        layout_result_destroy(layout_result);
        return NULL;
    }
    
    // Create SVG renderer
    SVGRenderer* renderer = svg_renderer_create(
        doc->page_settings->width,
        doc->page_settings->height
    );
    
    if (!renderer) {
        document_output_destroy(output);
        layout_result_destroy(layout_result);
        return NULL;
    }
    
    // Render each page
    for (int page_num = 1; page_num <= doc->page_count; page_num++) {
        // Reset renderer for new page
        svg_renderer_reset(renderer);
        
        // Render page content
        svg_render_document_page(renderer, doc, page_num);
        
        // Finalize SVG for this page
        String* svg_content = svg_renderer_finalize(renderer);
        
        // Create page output
        PageOutput* page = page_output_create(page_num, 
                                            doc->page_settings->width,
                                            doc->page_settings->height);
        page_output_set_svg_content(page, svg_content);
        
        // Add to document output
        document_output_add_page(output, page);
    }
    
    // Cleanup
    svg_renderer_destroy(renderer);
    layout_result_destroy(layout_result);
    
    // Calculate statistics
    calculate_output_statistics(output);
    
    return output;
}

void save_document_as_svg_pages(DocumentOutput* output, const char* base_filename) {
    if (!output || !base_filename) return;
    
    save_document_pages(output, base_filename);
}

void typeset_set_page_settings(TypesetEngine* engine, PageSettings* settings) {
    if (!engine || !settings) return;
    
    if (engine->default_page_settings) {
        page_settings_destroy(engine->default_page_settings);
    }
    
    engine->default_page_settings = page_settings_copy(settings);
}

void typeset_set_default_font(TypesetEngine* engine, const char* font_family, float size) {
    if (!engine || !font_family) return;
    
    font_manager_set_default_font(engine->font_manager, font_family, size);
}

void typeset_apply_stylesheet(TypesetEngine* engine, Document* doc, const char* css_like_rules) {
    if (!engine || !doc || !css_like_rules) return;
    
    // Parse CSS-like rules and create stylesheet
    StyleSheet* additional_sheet = parse_css_stylesheet(css_like_rules);
    if (!additional_sheet) return;
    
    // Merge with document's existing stylesheet
    if (doc->stylesheet) {
        stylesheet_merge(doc->stylesheet, additional_sheet);
        stylesheet_destroy(additional_sheet);
    } else {
        doc->stylesheet = additional_sheet;
    }
    
    // Re-apply styles to document
    apply_stylesheet_to_document(doc, doc->stylesheet);
}

// Lambda function integration
Item fn_typeset(Context* ctx, Item* args, int arg_count) {
    if (arg_count < 1) return ITEM_ERROR;
    
    Item input_item = args[0];
    Item options = arg_count > 1 ? args[1] : ITEM_NULL;
    
    // Create typesetting engine
    TypesetEngine* engine = typeset_engine_create(ctx);
    if (!engine) return ITEM_ERROR;
    
    // Apply options if provided
    if (get_type_id(options) == LMD_TYPE_MAP) {
        // Parse options from map
        // This would extract page size, fonts, etc. from the map
        // For now, we'll use defaults
    }
    
    // Create document from Lambda item
    Document* doc = typeset_from_lambda_item(engine, input_item);
    if (!doc) {
        typeset_engine_destroy(engine);
        return ITEM_ERROR;
    }
    
    // Render document to SVG pages
    DocumentOutput* output = render_document_to_svg(engine, doc);
    if (!output) {
        document_destroy(doc);
        typeset_engine_destroy(engine);
        return ITEM_ERROR;
    }
    
    // Convert SVG pages to Lambda list
    Item result = create_lambda_page_list(ctx, output);
    
    // Cleanup
    document_output_destroy(output);
    document_destroy(doc);
    typeset_engine_destroy(engine);
    
    return result;
}
