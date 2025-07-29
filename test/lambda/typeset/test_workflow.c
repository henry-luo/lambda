#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/strbuf.h"

// Mock Lambda item structure for proof of concept
typedef struct MockItem {
    char* tag;          // Element tag (e.g., "paragraph", "text", "math")
    char* content;      // Text content (if any)
    struct MockItem** children;  // Child elements
    int child_count;    // Number of children
    
    // Style attributes
    char* font_family;
    double font_size;
    char* color;
} MockItem;

// Mock typesetting engine structure
typedef struct MockTypesetEngine {
    double page_width;
    double page_height;
    double margin;
    StrBuf* output;
} MockTypesetEngine;

// Create a mock Lambda element tree
MockItem* create_mock_element(const char* tag, const char* content) {
    MockItem* item = calloc(1, sizeof(MockItem));
    item->tag = strdup(tag);
    if (content) {
        item->content = strdup(content);
    }
    item->children = NULL;
    item->child_count = 0;
    item->font_size = 12.0; // Default
    return item;
}

void add_child(MockItem* parent, MockItem* child) {
    parent->children = realloc(parent->children, (parent->child_count + 1) * sizeof(MockItem*));
    parent->children[parent->child_count] = child;
    parent->child_count++;
}

void free_mock_item(MockItem* item) {
    if (!item) return;
    
    free(item->tag);
    free(item->content);
    free(item->font_family);
    free(item->color);
    
    for (int i = 0; i < item->child_count; i++) {
        free_mock_item(item->children[i]);
    }
    free(item->children);
    free(item);
}

// Print mock Lambda element tree (simulating print.c)
void print_mock_item(MockItem* item, int indent) {
    if (!item) return;
    
    // Print indentation
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    
    // Print element
    printf("<%s", item->tag);
    if (item->font_family) {
        printf(" font-family=\"%s\"", item->font_family);
    }
    if (item->font_size != 12.0) {
        printf(" font-size=\"%.1f\"", item->font_size);
    }
    if (item->color) {
        printf(" color=\"%s\"", item->color);
    }
    
    if (item->content) {
        printf(">%s</%s>\n", item->content, item->tag);
    } else if (item->child_count > 0) {
        printf(">\n");
        for (int i = 0; i < item->child_count; i++) {
            print_mock_item(item->children[i], indent + 1);
        }
        for (int i = 0; i < indent; i++) {
            printf("  ");
        }
        printf("</%s>\n", item->tag);
    } else {
        printf("/>\n");
    }
}

// Convert mock Lambda element tree to SVG (simulating typesetting)
void render_mock_item_to_svg(MockTypesetEngine* engine, MockItem* item, double* x, double* y) {
    if (!item || !engine || !engine->output) return;
    
    if (strcmp(item->tag, "document") == 0) {
        // Document root - render children
        for (int i = 0; i < item->child_count; i++) {
            render_mock_item_to_svg(engine, item->children[i], x, y);
        }
    } else if (strcmp(item->tag, "paragraph") == 0) {
        // Paragraph - add some vertical spacing
        *y += 20.0;
        for (int i = 0; i < item->child_count; i++) {
            render_mock_item_to_svg(engine, item->children[i], x, y);
        }
        *y += 10.0; // Paragraph spacing
    } else if (strcmp(item->tag, "text") == 0 && item->content) {
        // Text content - render as SVG text element
        double font_size = item->font_size;
        const char* font_family = item->font_family ? item->font_family : "Arial";
        const char* color = item->color ? item->color : "black";
        
        strbuf_append_format(engine->output,
            "  <text x=\"%.1f\" y=\"%.1f\" font-family=\"%s\" font-size=\"%.1f\" fill=\"%s\">%s</text>\n",
            *x, *y, font_family, font_size, color, item->content);
        
        // Advance position (simplified)
        *x += strlen(item->content) * font_size * 0.6; // Approximate character width
        
        // Wrap to next line if needed
        if (*x > engine->page_width - engine->margin * 2) {
            *x = engine->margin;
            *y += font_size * 1.2;
        }
    } else if (strcmp(item->tag, "heading") == 0 && item->content) {
        // Heading - larger font
        double font_size = item->font_size * 1.5;
        *y += font_size * 0.5; // Extra space before heading
        
        strbuf_append_format(engine->output,
            "  <text x=\"%.1f\" y=\"%.1f\" font-family=\"Arial\" font-size=\"%.1f\" font-weight=\"bold\" fill=\"black\">%s</text>\n",
            *x, *y, font_size, item->content);
        
        *y += font_size * 1.3; // Extra space after heading
    } else if (strcmp(item->tag, "math") == 0 && item->content) {
        // Math expression - render in italic
        strbuf_append_format(engine->output,
            "  <text x=\"%.1f\" y=\"%.1f\" font-family=\"Times\" font-size=\"%.1f\" font-style=\"italic\" fill=\"blue\">%s</text>\n",
            *x, *y, item->font_size, item->content);
        
        *x += strlen(item->content) * item->font_size * 0.7; // Math is typically wider
    }
}

// Create mock typesetting engine
MockTypesetEngine* create_mock_engine(void) {
    MockTypesetEngine* engine = calloc(1, sizeof(MockTypesetEngine));
    engine->page_width = 612.0;
    engine->page_height = 792.0;
    engine->margin = 72.0;
    engine->output = strbuf_new();
    return engine;
}

void destroy_mock_engine(MockTypesetEngine* engine) {
    if (!engine) return;
    strbuf_free(engine->output);
    free(engine);
}

int main() {
    printf("=== Lambda Typesetting Workflow Demonstration ===\n");
    
    // Step 1: Create a Lambda element tree (simulating input parser output)
    printf("\n1. Creating Lambda element tree (simulating input parser)...\n");
    
    MockItem* document = create_mock_element("document", NULL);
    
    MockItem* title = create_mock_element("heading", "Lambda Typesetting System");
    title->font_size = 18.0;
    add_child(document, title);
    
    MockItem* intro = create_mock_element("paragraph", NULL);
    MockItem* intro_text = create_mock_element("text", "This document demonstrates the Lambda typesetting workflow: ");
    add_child(intro, intro_text);
    MockItem* emphasis = create_mock_element("text", "input parsing → element tree → view tree → SVG output");
    emphasis->color = strdup("darkblue");
    emphasis->font_family = strdup("Times");
    add_child(intro, emphasis);
    add_child(document, intro);
    
    MockItem* math_para = create_mock_element("paragraph", NULL);
    MockItem* math_intro = create_mock_element("text", "Mathematical expressions: ");
    add_child(math_para, math_intro);
    MockItem* math_expr = create_mock_element("math", "f(x) = x² + 2x + 1");
    add_child(math_para, math_expr);
    add_child(document, math_para);
    
    printf("Lambda element tree created with %d top-level elements\n", document->child_count);
    
    // Step 2: Print the Lambda element tree (simulating print.c)
    printf("\n2. Printing Lambda element tree (simulating print.c)...\n");
    print_mock_item(document, 0);
    
    // Step 3: Typeset the element tree to view tree and render as SVG
    printf("\n3. Typesetting to view tree and rendering as SVG...\n");
    
    MockTypesetEngine* engine = create_mock_engine();
    
    // SVG header
    strbuf_append_str(engine->output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_append_format(engine->output, "<svg width=\"%.0f\" height=\"%.0f\" xmlns=\"http://www.w3.org/2000/svg\">\n", 
                        engine->page_width, engine->page_height);
    strbuf_append_str(engine->output, "  <title>Lambda Typesetting Demonstration</title>\n");
    
    // Render content
    double x = engine->margin;
    double y = engine->margin + 20.0; // Start position
    render_mock_item_to_svg(engine, document, &x, &y);
    
    // SVG footer
    strbuf_append_str(engine->output, "</svg>\n");
    
    printf("SVG rendering complete. Length: %zu characters\n", engine->output->length);
    
    // Step 4: Output results
    printf("\n4. Writing output files...\n");
    
    // Write SVG file
    FILE* svg_file = fopen("lambda_typeset_demo.svg", "w");
    if (svg_file) {
        fprintf(svg_file, "%s", engine->output->str);
        fclose(svg_file);
        printf("SVG written to lambda_typeset_demo.svg\n");
    }
    
    // Write a simple HTML preview
    FILE* html_file = fopen("lambda_typeset_demo.html", "w");
    if (html_file) {
        fprintf(html_file, "<!DOCTYPE html>\n<html><head><title>Lambda Typesetting Demo</title></head>\n");
        fprintf(html_file, "<body style=\"font-family: Arial; margin: 20px;\">\n");
        fprintf(html_file, "<h1>Lambda Typesetting System Demo</h1>\n");
        fprintf(html_file, "<p>This demonstrates the complete workflow:</p>\n");
        fprintf(html_file, "<ol>\n");
        fprintf(html_file, "<li>Input parsing → Lambda element tree</li>\n");
        fprintf(html_file, "<li>Element tree printing (via print.c)</li>\n");
        fprintf(html_file, "<li>Typesetting → device-independent view tree</li>\n");
        fprintf(html_file, "<li>Rendering → SVG output</li>\n");
        fprintf(html_file, "</ol>\n");
        fprintf(html_file, "<h2>Generated SVG:</h2>\n");
        fprintf(html_file, "%s", engine->output->str);
        fprintf(html_file, "</body></html>\n");
        fclose(html_file);
        printf("HTML preview written to lambda_typeset_demo.html\n");
    }
    
    // Print SVG preview
    printf("\n5. SVG content preview:\n");
    printf("%.400s", engine->output->str);
    if (engine->output->length > 400) {
        printf("...\n");
    }
    
    printf("\n=== Summary ===\n");
    printf("✓ Lambda element tree creation (input parser simulation)\n");
    printf("✓ Element tree printing (print.c simulation)\n");
    printf("✓ View tree generation and SVG rendering (typesetting)\n");
    printf("✓ Multi-format output (SVG + HTML preview)\n");
    printf("✓ Typographic calculations and layout\n");
    
    printf("\nThis demonstrates the complete Lambda typesetting pipeline!\n");
    printf("Next steps: integrate with actual Lambda runtime and input parsers.\n");
    
    // Cleanup
    free_mock_item(document);
    destroy_mock_engine(engine);
    
    return 0;
}
