#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include only the core typesetting headers we need
#include "typeset/view/view_tree.h"
#include "typeset/output/renderer.h"
#include "lib/strbuf.h"

// Simple test to create a basic view tree and render it to SVG
int main() {
    printf("=== Minimal Typesetting Test ===\n");
    
    // Create a minimal view tree
    ViewTree* tree = view_tree_create();
    if (!tree) {
        printf("Failed to create view tree\n");
        return 1;
    }
    
    tree->document_size.width = 612.0;  // Letter size width
    tree->document_size.height = 792.0; // Letter size height
    tree->page_count = 1;
    tree->pages = calloc(1, sizeof(ViewPage*));
    tree->pages[0] = calloc(1, sizeof(ViewPage));
    tree->pages[0]->page_size.width = 612.0;
    tree->pages[0]->page_size.height = 792.0;
    
    // Create a simple text node
    ViewNode* text_node = calloc(1, sizeof(ViewNode));
    text_node->type = VIEW_NODE_TEXT_RUN;
    text_node->position.x = 50.0;
    text_node->position.y = 100.0;
    text_node->size.width = 200.0;
    text_node->size.height = 20.0;
    text_node->visible = true;
    text_node->opacity = 1.0;
    
    // Create text run content
    text_node->content.text_run = calloc(1, sizeof(ViewTextRun));
    text_node->content.text_run->text = strdup("Hello, Typesetting!");
    text_node->content.text_run->text_length = strlen(text_node->content.text_run->text);
    text_node->content.text_run->font_size = 12.0;
    text_node->content.text_run->color.r = 0.0;
    text_node->content.text_run->color.g = 0.0;
    text_node->content.text_run->color.b = 0.0;
    text_node->content.text_run->color.a = 1.0;
    
    tree->pages[0]->page_node = text_node;
    tree->root = text_node;  // Also set as tree root
    
    printf("Created view tree with text node: \"%s\"\n", text_node->content.text_run->text);
    
    // Create view renderer
    ViewRenderer* renderer = view_renderer_create();
    if (!renderer) {
        printf("Failed to create view renderer\n");
        return 1;
    }
    
    printf("View renderer created successfully\n");
    
    // Create basic render options
    ViewRenderOptions options = {0};
    options.format = VIEW_FORMAT_SVG;
    options.page_width = 612.0;
    options.page_height = 792.0;
    
    // Render to SVG
    StrBuf* svg_output = render_view_tree_to_svg(tree, &options);
    if (!svg_output) {
        printf("Failed to render SVG\n");
        return 1;
    }
    
    printf("SVG rendering complete. Output length: %zu\n", svg_output->length);
    printf("SVG content preview:\n%.200s...\n", svg_output->str);
    
    // Write to file
    FILE* file = fopen("test_output.svg", "w");
    if (file) {
        fprintf(file, "%s", svg_output->str);
        fclose(file);
        printf("SVG written to test_output.svg\n");
    }
    
    // Cleanup
    view_renderer_destroy(renderer);
    free(text_node->content.text_run->text);
    free(text_node->content.text_run);
    free(text_node);
    free(tree->pages[0]);
    free(tree->pages);
    view_tree_destroy(tree);
    
    printf("Test completed successfully!\n");
    return 0;
}
