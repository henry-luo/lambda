#include "typeset.h"
#include <stdlib.h>
#include <string.h>

// Main typesetting engine implementation

TypesetEngine* typeset_engine_create(Context* ctx) {
    TypesetEngine* engine = malloc(sizeof(TypesetEngine));
    if (!engine) return NULL;
    
    engine->lambda_context = ctx;
    engine->font_manager = font_manager_create();
    engine->default_stylesheet = create_default_document_stylesheet(engine->font_manager);
    engine->default_page_settings = page_settings_create();
    
    if (!engine->font_manager || !engine->default_stylesheet || !engine->default_page_settings) {
        typeset_engine_destroy(engine);
        return NULL;
    }
    
    // Set up default page settings (A4)
    page_settings_set_a4(engine->default_page_settings);
    page_settings_set_margins(engine->default_page_settings, 
                             TYPESET_DEFAULT_MARGIN, TYPESET_DEFAULT_MARGIN,
                             TYPESET_DEFAULT_MARGIN, TYPESET_DEFAULT_MARGIN);
    
    return engine;
}

void typeset_engine_destroy(TypesetEngine* engine) {
    if (!engine) return;
    
    if (engine->font_manager) {
        font_manager_destroy(engine->font_manager);
    }
    
    if (engine->default_stylesheet) {
        stylesheet_destroy(engine->default_stylesheet);
    }
    
    if (engine->default_page_settings) {
        page_settings_destroy(engine->default_page_settings);
    }
    
    free(engine);
}

Document* typeset_from_lambda_item(TypesetEngine* engine, Item root_item) {
    if (!engine || !root_item) return NULL;
    
    // Create document from Lambda item using the bridge
    Document* doc = create_document_from_lambda_item(engine, root_item);
    if (!doc) return NULL;
    
    // Apply default page settings if not already set
    if (!doc->page_settings) {
        doc->page_settings = page_settings_copy(engine->default_page_settings);
    }
    
    // Apply default stylesheet if not already set
    if (!doc->stylesheet) {
        doc->stylesheet = stylesheet_copy(engine->default_stylesheet);
    }
    
    // Set font manager reference
    doc->font_manager = engine->font_manager;
    
    // Apply stylesheet to document
    apply_stylesheet_to_document(doc, doc->stylesheet);
    
    return doc;
}

Document* typeset_from_markdown(TypesetEngine* engine, const char* markdown) {
    if (!engine || !markdown) return NULL;
    
    // Parse markdown using existing Lambda input system
    // This would use the existing input-md.c parser
    // For now, we'll create a simple document structure
    
    Document* doc = document_create(engine->lambda_context);
    if (!doc) return NULL;
    
    // Set up document with default settings
    doc->page_settings = page_settings_copy(engine->default_page_settings);
    doc->stylesheet = stylesheet_copy(engine->default_stylesheet);
    doc->font_manager = engine->font_manager;
    
    // Create a simple text paragraph for the markdown content
    DocNode* paragraph = create_paragraph_node();
    DocNode* text = create_text_node(markdown);
    docnode_append_child(paragraph, text);
    docnode_append_child(doc->root, paragraph);
    
    return doc;
}

Document* typeset_from_latex(TypesetEngine* engine, const char* latex) {
    if (!engine || !latex) return NULL;
    
    // Parse LaTeX using existing Lambda input system
    // This would use the existing input-latex.c parser
    // For now, we'll create a simple document structure
    
    Document* doc = document_create(engine->lambda_context);
    if (!doc) return NULL;
    
    // Set up document with default settings
    doc->page_settings = page_settings_copy(engine->default_page_settings);
    doc->stylesheet = stylesheet_copy(engine->default_stylesheet);
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
