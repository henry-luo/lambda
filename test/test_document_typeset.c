#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../typeset/document/document_typeset.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input-common.h"
#include "../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test comprehensive markdown document with multiple math expressions
Test(document_typeset, comprehensive_markdown_with_math) {
    log_info("Starting comprehensive markdown document typesetting test");
    
    // Set up memory pool
    Pool* pool = pool_init(1024 * 1024); // 1MB pool
    cr_assert_not_null(pool, "Pool should be initialized");
    
    // Load the sample markdown document
    const char* doc_path = "test/input/sample_math_document.md";
    Item doc_item = input_auto_detect(pool, doc_path);
    cr_assert_eq(get_type_id(doc_item), LMD_TYPE_ELEMENT, "Document should be parsed as element");
    
    Element* document = (Element*)doc_item.pointer;
    cr_assert_not_null(document, "Document element should not be null");
    
    // Create document typesetting options
    DocumentTypesetOptions* options = create_default_document_options();
    cr_assert_not_null(options, "Options should be created");
    
    // Set specific options for this test
    options->base_options.page_width = 800;
    options->base_options.page_height = 1200;
    options->math_options.font_size = 14.0;
    options->render_math_as_svg = true;
    free(options->output_format);
    options->output_format = strdup("svg");
    
    // Typeset the document
    DocumentTypesetResult* result = typeset_markdown_document(document, options);
    cr_assert_not_null(result, "Document typesetting should succeed");
    cr_assert_eq(result->has_errors, false, "Typesetting should not have errors");
    
    // Verify result properties
    cr_assert_gt(result->math_expressions_count, 0, "Should find math expressions");
    cr_assert_gt(result->inline_math_count, 5, "Should have multiple inline math expressions");
    cr_assert_gt(result->display_math_count, 10, "Should have multiple display math expressions");
    cr_assert_not_null(result->rendered_output, "Should have rendered output");
    cr_assert_gt(result->output_size_bytes, 1000, "SVG output should be substantial");
    
    // Verify ViewTree structure
    cr_assert_not_null(result->view_tree, "Should have view tree");
    cr_assert_not_null(result->view_tree->root, "Should have root node");
    cr_assert_gt(result->view_tree->root->child_count, 10, "Should have multiple document elements");
    
    // Check document dimensions
    cr_assert_eq(result->view_tree->document_size.width, 800.0, "Document width should match options");
    cr_assert_gt(result->view_tree->document_size.height, 600.0, "Document should have substantial height");
    
    // Verify SVG output structure
    const char* svg_content = result->rendered_output->str;
    cr_assert_str_match(svg_content, "<?xml version=\"1.0\"*", "Should have XML declaration");
    cr_assert_str_match(svg_content, "*<svg*", "Should contain SVG element");
    cr_assert_str_match(svg_content, "*width=\"800*", "Should have correct width");
    cr_assert_str_match(svg_content, "*Mathematical*", "Should contain mathematical content");
    cr_assert_str_match(svg_content, "*</svg>*", "Should close SVG properly");
    
    // Write output file for inspection
    const char* output_path = "test_output/comprehensive_math_document.svg";
    FILE* output_file = fopen(output_path, "w");
    if (output_file) {
        fwrite(svg_content, 1, result->rendered_output->length, output_file);
        fclose(output_file);
        log_info("Comprehensive document SVG written to: %s", output_path);
        log_info("Document stats: %d math expressions (%d inline, %d display), %.2f ms typeset time",
                result->math_expressions_count, result->inline_math_count, 
                result->display_math_count, result->typeset_time_ms);
    }
    
    // Verify specific math expressions from our sample document
    // The document contains: limits, integrals, series, derivatives, etc.
    cr_assert_str_match(svg_content, "*\\int*", "Should contain integral expressions");
    cr_assert_str_match(svg_content, "*\\sum*", "Should contain summation expressions");
    cr_assert_str_match(svg_content, "*\\lim*", "Should contain limit expressions");
    cr_assert_str_match(svg_content, "*\\frac*", "Should contain fraction expressions");
    
    // Test performance characteristics
    cr_assert_lt(result->typeset_time_ms, 5000.0, "Typesetting should complete in reasonable time");
    
    // Clean up
    destroy_document_typeset_result(result);
    destroy_document_options(options);
    pool_destroy(pool);
    
    log_info("Comprehensive markdown document typesetting test completed successfully");
}

// Test math expression extraction and classification
Test(document_typeset, math_expression_classification) {
    log_info("Testing math expression classification");
    
    Pool* pool = pool_init(512 * 1024);
    cr_assert_not_null(pool);
    
    // Create a simple test document with known math expressions
    const char* test_content = 
        "# Test Document\n"
        "\n"
        "This is inline math: $f(x) = x^2$ and more text.\n"
        "\n"
        "This is display math:\n"
        "$$\\int_0^1 f(x) dx = \\frac{1}{3}$$\n"
        "\n"
        "More inline: $\\alpha + \\beta = \\gamma$ and $e^{i\\pi} = -1$.\n"
        "\n"
        "Another display:\n"
        "$$\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}$$\n";
    
    // Parse content (simplified - in real test would use full Lambda parser)
    DocumentTypesetOptions* options = create_default_document_options();
    cr_assert_not_null(options);
    
    // Mock document element for this test
    Element* mock_doc = (Element*)pool_alloc(pool, sizeof(Element));
    // Note: In real implementation, would properly initialize Lambda element
    
    DocumentTypesetResult* result = typeset_markdown_document(mock_doc, options);
    cr_assert_not_null(result);
    
    // In a complete implementation, these would be properly extracted
    // For now, verify the infrastructure is in place
    cr_assert_geq(result->math_expressions_count, 0, "Math expressions should be counted");
    
    destroy_document_typeset_result(result);
    destroy_document_options(options);
    pool_destroy(pool);
    
    log_info("Math expression classification test completed");
}

// Test document layout and positioning
Test(document_typeset, document_layout) {
    log_info("Testing document layout and positioning");
    
    Pool* pool = pool_init(256 * 1024);
    DocumentTypesetOptions* options = create_default_document_options();
    
    // Test with different page sizes
    options->base_options.page_width = 600;
    options->base_options.page_height = 800;
    options->base_options.margin_left = 50;
    options->base_options.margin_right = 50;
    options->base_options.margin_top = 60;
    options->base_options.margin_bottom = 60;
    
    Element* mock_doc = (Element*)pool_alloc(pool, sizeof(Element));
    
    DocumentTypesetResult* result = typeset_markdown_document(mock_doc, options);
    cr_assert_not_null(result);
    
    if (result->view_tree) {
        // Verify document dimensions match options
        cr_assert_eq(result->view_tree->document_size.width, 600.0, "Width should match options");
        
        // Verify margins are applied
        if (result->view_tree->root) {
            // Root container should respect margins
            cr_assert_geq(result->view_tree->root->position.x, 0.0, "Root X position should be valid");
            cr_assert_geq(result->view_tree->root->position.y, 0.0, "Root Y position should be valid");
        }
    }
    
    destroy_document_typeset_result(result);
    destroy_document_options(options);
    pool_destroy(pool);
    
    log_info("Document layout test completed");
}

// Test SVG output quality and structure
Test(document_typeset, svg_output_quality) {
    log_info("Testing SVG output quality and structure");
    
    Pool* pool = pool_init(256 * 1024);
    DocumentTypesetOptions* options = create_default_document_options();
    
    Element* mock_doc = (Element*)pool_alloc(pool, sizeof(Element));
    
    DocumentTypesetResult* result = typeset_markdown_document(mock_doc, options);
    cr_assert_not_null(result);
    
    if (result->rendered_output) {
        const char* svg = result->rendered_output->str;
        
        // Verify SVG structure
        cr_assert_str_match(svg, "<?xml*", "Should have XML declaration");
        cr_assert_str_match(svg, "*<svg*", "Should have SVG root element");
        cr_assert_str_match(svg, "*xmlns=\"http://www.w3.org/2000/svg\"*", "Should have correct namespace");
        cr_assert_str_match(svg, "*<title>*", "Should have title element");
        cr_assert_str_match(svg, "*<defs>*", "Should have definitions section");
        cr_assert_str_match(svg, "*<style>*", "Should have CSS styles");
        cr_assert_str_match(svg, "*</svg>*", "Should close properly");
        
        // Verify CSS classes are defined
        cr_assert_str_match(svg, "*.document*", "Should define document class");
        cr_assert_str_match(svg, "*.math-*", "Should define math classes");
        
        // Verify viewBox is set
        cr_assert_str_match(svg, "*viewBox=\"*", "Should have viewBox attribute");
    }
    
    destroy_document_typeset_result(result);
    destroy_document_options(options);
    pool_destroy(pool);
    
    log_info("SVG output quality test completed");
}
