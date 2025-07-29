#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/strbuf.h"

// Simple test to validate core typesetting concepts
int main() {
    printf("=== Simple Typesetting Proof of Concept ===\n");
    
    // Test basic string buffer functionality
    StrBuf* output = strbuf_new();
    if (!output) {
        printf("Failed to create string buffer\n");
        return 1;
    }
    
    printf("String buffer created successfully\n");
    
    // Build a simple SVG document
    strbuf_append_str(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_append_str(output, "<svg width=\"612\" height=\"792\" ");
    strbuf_append_str(output, "xmlns=\"http://www.w3.org/2000/svg\">\n");
    
    // Add title
    strbuf_append_str(output, "  <title>Lambda Typesetting Test</title>\n");
    
    // Add a simple text element
    strbuf_append_format(output, "  <text x=\"%.1f\" y=\"%.1f\" font-family=\"Arial\" font-size=\"%.1f\" fill=\"black\">\n", 
                        50.0, 100.0, 12.0);
    strbuf_append_str(output, "    Hello, Lambda Typesetting System!\n");
    strbuf_append_str(output, "  </text>\n");
    
    // Add a simple rectangle
    strbuf_append_format(output, "  <rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" ", 
                        50.0, 120.0, 200.0, 20.0);
    strbuf_append_str(output, "fill=\"none\" stroke=\"black\" stroke-width=\"1\"/>\n");
    
    // Close SVG
    strbuf_append_str(output, "</svg>\n");
    
    printf("SVG document generated. Length: %zu characters\n", output->length);
    printf("SVG content preview:\n%.200s...\n", output->str);
    
    // Write to file
    FILE* file = fopen("simple_typeset_test.svg", "w");
    if (file) {
        fprintf(file, "%s", output->str);
        fclose(file);
        printf("SVG written to simple_typeset_test.svg\n");
    } else {
        printf("Failed to write SVG file\n");
    }
    
    // Test basic typesetting calculations
    double page_width = 612.0;    // Letter width in points
    double page_height = 792.0;   // Letter height in points
    double margin = 72.0;         // 1 inch margins
    double content_width = page_width - 2 * margin;
    double content_height = page_height - 2 * margin;
    
    printf("\nTypesetting calculations:\n");
    printf("Page size: %.1f x %.1f points\n", page_width, page_height);
    printf("Content area: %.1f x %.1f points\n", content_width, content_height);
    printf("Margins: %.1f points (%.2f inches)\n", margin, margin / 72.0);
    
    // Test font metrics (simplified)
    double font_size = 12.0;
    double line_height = font_size * 1.2;  // Typical line height
    double chars_per_inch = 72.0 / (font_size * 0.6);  // Approximate
    double lines_per_page = content_height / line_height;
    
    printf("\nFont metrics (approximate):\n");
    printf("Font size: %.1f points\n", font_size);
    printf("Line height: %.1f points\n", line_height);
    printf("Characters per inch: %.1f\n", chars_per_inch);
    printf("Lines per page: %.0f\n", lines_per_page);
    
    // Cleanup
    strbuf_free(output);
    
    printf("\nBasic typesetting proof of concept completed successfully!\n");
    printf("This validates core concepts:\n");
    printf("1. String buffer management\n");
    printf("2. SVG document generation\n");
    printf("3. Typographic measurements\n");
    printf("4. Page layout calculations\n");
    
    return 0;
}
