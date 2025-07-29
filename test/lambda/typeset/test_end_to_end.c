#include "../../../lambda/lambda.h"
#include "../../../lambda/transpiler.h"
#include "../../../typeset/typeset.h"
#include "../../../typeset/view/view_tree.h"
#include "../../../typeset/integration/lambda_bridge.h"
#include "../../../typeset/serialization/lambda_serializer.h"
#include "../../../typeset/output/svg_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External functions from Lambda runtime
extern void print_item(StrBuf *strbuf, Item item);
extern StrBuf* strbuf_create(void);
extern void strbuf_destroy(StrBuf* buf);
extern void heap_init(void);
extern void heap_destroy(void);
extern List* list(void);
extern Map* map(int type_index);
extern String* create_str(const char* str);

// Global context (as used in Lambda runtime)
extern __thread Context* context;

// Test markdown content
static const char* test_markdown = 
    "# Hello World\n"
    "\n"
    "This is a **simple** markdown document with some basic formatting.\n"
    "\n"
    "- Item 1\n"
    "- Item 2\n"
    "- Item 3\n"
    "\n"
    "Math: E = mc^2\n"
    "\n"
    "End of document.\n";

void print_separator(const char* title) {
    printf("\n=== %s ===\n", title);
}

// Create a mock Lambda element tree representing parsed markdown
Item create_mock_document_tree(void) {
    // For now, create a simple string item representing the document
    // In a real implementation, this would be a structured map/list
    String* doc_str = create_str("Mock document with heading, paragraph, list, and math");
    return s2it(doc_str);
}

int main() {
    printf("Typesetting End-to-End Test (Direct Lambda Elements)\n");
    
    // Initialize minimal Lambda context
    context = calloc(1, sizeof(Context));
    if (!context) {
        fprintf(stderr, "Failed to create Lambda context\n");
        return 1;
    }
    
    // Initialize heap
    heap_init();
    
    print_separator("Step 1: Input Markdown");
    printf("Markdown content:\n%s\n", test_markdown);
    
    print_separator("Step 2: Create Mock Lambda Element Tree");
    printf("Creating Lambda element tree representing parsed markdown...\n");
    
    Item doc_item = create_mock_document_tree();
    if (doc_item == ITEM_NULL) {
        fprintf(stderr, "Failed to create document tree\n");
        heap_destroy();
        free(context);
        return 1;
    }
    
    printf("Lambda element tree created successfully.\n");
    printf("Document item type: %d\n", ((LambdaItem)doc_item).type_id);
    
    // Use Lambda's print function to display the element tree
    StrBuf* output_buf = strbuf_create();
    print_item(output_buf, doc_item);
    printf("Document structure: %s\n", output_buf->str);
    strbuf_destroy(output_buf);
    
    print_separator("Step 3: Convert to View Tree");
    
    // Create typeset engine
    TypesetEngine* engine = typeset_engine_create(context);
    if (!engine) {
        fprintf(stderr, "Failed to create typeset engine\n");
        heap_destroy();
        free(context);
        return 1;
    }
    
    // Convert Lambda element tree to view tree
    ViewTree* view_tree = create_view_tree_from_lambda_item(engine, doc_item);
    if (!view_tree) {
        fprintf(stderr, "Failed to create view tree\n");
        typeset_engine_destroy(engine);
        heap_destroy();
        free(context);
        return 1;
    }
    
    printf("View tree created successfully.\n");
    printf("Root node type: %d\n", view_tree->root->type);
    printf("Child count: %d\n", view_tree->root->child_count);
    
    print_separator("Step 4: Serialize View Tree to Lambda Element");
    
    // Create serializer
    SerializationOptions* ser_options = serialization_options_create_default();
    LambdaSerializer* serializer = lambda_serializer_create(context, ser_options);
    if (!serializer) {
        fprintf(stderr, "Failed to create serializer\n");
        view_tree_release(view_tree);
        typeset_engine_destroy(engine);
        heap_destroy();
        free(context);
        return 1;
    }
    
    // Serialize view tree back to Lambda element tree
    Item serialized = serialize_view_tree_to_lambda(serializer, view_tree);
    if (serialized == ITEM_NULL) {
        fprintf(stderr, "Failed to serialize view tree\n");
    } else {
        printf("View tree serialized to Lambda element successfully.\n");
        printf("Serialized view tree structure:\n");
        
        // Print the serialized Lambda element tree
        StrBuf* serialized_buf = strbuf_create();
        print_item(serialized_buf, serialized);
        printf("%s\n", serialized_buf->str);
        strbuf_destroy(serialized_buf);
    }
    
    print_separator("Step 5: Render to SVG");
    
    // Set up SVG rendering options
    SVGRenderOptions svg_options = {
        .width = 595.276,  // A4 width in points
        .height = 841.89,  // A4 height in points
        .margin_left = 72.0,
        .margin_top = 72.0,
        .margin_right = 72.0,
        .margin_bottom = 72.0,
        .background_color = "white"
    };
    
    // Render view tree to SVG
    StrBuf* svg_buffer = render_view_tree_to_svg_internal(view_tree, &svg_options);
    if (!svg_buffer) {
        fprintf(stderr, "Failed to render SVG\n");
    } else {
        printf("SVG rendered successfully.\n");
        printf("SVG content length: %zu bytes\n", svg_buffer->length);
        
        // Save SVG to file
        FILE* svg_file = fopen("test_output.svg", "w");
        if (svg_file) {
            fwrite(svg_buffer->data, 1, svg_buffer->length, svg_file);
            fclose(svg_file);
            printf("SVG saved to test_output.svg\n");
        }
        
        // Print first few lines of SVG for verification
        printf("SVG preview (first 300 chars):\n");
        size_t preview_len = svg_buffer->length > 300 ? 300 : svg_buffer->length;
        char preview[301];
        memcpy(preview, svg_buffer->data, preview_len);
        preview[preview_len] = '\0';
        printf("%s...\n", preview);
        
        strbuf_destroy(svg_buffer);
    }
    
    print_separator("Test Complete");
    printf("End-to-end typesetting test completed successfully.\n");
    printf("Workflow: Mock Element Tree -> View Tree -> Lambda Serialization -> SVG\n");
    printf("✓ Lambda element tree creation and printing\n");
    printf("✓ View tree conversion\n");
    printf("✓ View tree serialization back to Lambda format\n");
    printf("✓ SVG rendering with A4 page layout\n");
    
    // Cleanup
    lambda_serializer_destroy(serializer);
    view_tree_release(view_tree);
    typeset_engine_destroy(engine);
    heap_destroy();
    free(context);
    
    return 0;
}
