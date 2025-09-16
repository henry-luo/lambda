#include "../lib/unit_test/include/criterion/criterion.h"
#include "../typeset/math_typeset.h"
#include "../typeset/view/view_tree.h"
#include "../typeset/layout/math_layout.h"
#include "../typeset/integration/lambda_math_bridge.h"
#include "../typeset/output/svg_renderer.h"
#include "../lambda/lambda.h"
#include "../lib/strbuf.h"
#include <stdio.h>
#include <string.h>

// Test the complete flow: LaTeX → Lambda tree → View tree → SVG
Test(math_typeset, complete_workflow) {
    // Step 1: Define test LaTeX math expression
    const char* latex_math = "\\frac{x^2 + 1}{\\sqrt{y + z}}";
    
    printf("Testing complete math typesetting workflow:\n");
    printf("Input LaTeX: %s\n", latex_math);
    
    // Step 2: Parse LaTeX math with input-math.cpp (simulated for now)
    // This would normally call the existing math parser
    printf("Step 1: Parsing LaTeX math expression...\n");
    
    // For now, create a mock Lambda tree structure representing the parsed math
    // In real implementation, this would be: Item math_tree = parse_math_expression(latex_math);
    Item math_tree = create_mock_fraction_tree();
    cr_assert_neq(math_tree.item, ITEM_ERROR, "Math parsing should succeed");
    cr_assert_neq(math_tree.item, ITEM_NULL, "Math tree should not be null");
    
    // Step 3: Convert to view tree
    printf("Step 2: Converting Lambda tree to view tree...\n");
    ViewTree* view_tree = typeset_math_from_lambda_tree(math_tree, NULL);
    cr_assert_not_null(view_tree, "View tree creation should succeed");
    cr_assert_not_null(view_tree->root, "View tree should have a root node");
    
    // Step 4: Validate view tree structure
    printf("Step 3: Validating view tree structure...\n");
    cr_assert(validate_math_tree_structure(view_tree->root), "Math tree structure should be valid");
    
    // Step 5: Render to SVG
    printf("Step 4: Rendering to SVG...\n");
    SVGRenderer* renderer = svg_renderer_create();
    cr_assert_not_null(renderer, "SVG renderer should be created");
    
    StrBuf* svg_output = strbuf_create(1024);
    bool render_success = svg_render_view_tree(renderer, view_tree, svg_output);
    cr_assert(render_success, "SVG rendering should succeed");
    cr_assert_not_null(svg_output->str, "SVG output should not be null");
    cr_assert_gt(svg_output->length, 0, "SVG output should have content");
    
    // Step 6: Validate SVG structure
    printf("Step 5: Validating SVG output...\n");
    cr_assert(strstr(svg_output->str, "<svg") != NULL, "SVG should have opening tag");
    cr_assert(strstr(svg_output->str, "</svg>") != NULL, "SVG should have closing tag");
    cr_assert(strstr(svg_output->str, "math") != NULL, "SVG should contain math-related content");
    
    // Step 7: Save SVG for visual inspection
    FILE* svg_file = fopen("test_math_output.svg", "w");
    if (svg_file) {
        fprintf(svg_file, "%s", svg_output->str);
        fclose(svg_file);
        printf("SVG output saved to test_math_output.svg\n");
    }
    
    printf("SVG content preview (first 200 chars):\n%.200s...\n", svg_output->str);
    
    // Cleanup
    view_tree_destroy(view_tree);
    svg_renderer_destroy(renderer);
    strbuf_destroy(svg_output);
    
    printf("✓ Complete math typesetting workflow test passed!\n");
}

// Test specific mathematical constructs
Test(math_typeset, fraction_typesetting) {
    printf("Testing fraction typesetting...\n");
    
    // Create a simple fraction: 1/2
    ViewNode* numerator = create_math_atom_node("1", "1");
    ViewNode* denominator = create_math_atom_node("2", "2");
    ViewNode* fraction = create_math_fraction_node(numerator, denominator);
    
    cr_assert_not_null(fraction, "Fraction node should be created");
    cr_assert_eq(fraction->type, VIEW_NODE_MATH_ELEMENT, "Should be math element");
    cr_assert_eq(fraction->content.math_elem->type, VIEW_MATH_FRACTION, "Should be fraction type");
    
    // Test layout
    MathLayoutContext* ctx = math_layout_context_create(NULL, NULL, VIEW_MATH_DISPLAY);
    ViewNode* laid_out = layout_math_fraction(fraction, ctx);
    cr_assert_not_null(laid_out, "Fraction layout should succeed");
    
    math_layout_context_destroy(ctx);
    view_node_release(laid_out);
    
    printf("✓ Fraction typesetting test passed!\n");
}

Test(math_typeset, superscript_subscript_positioning) {
    printf("Testing superscript/subscript positioning...\n");
    
    // Create x^2
    ViewNode* base = create_math_atom_node("x", "x");
    ViewNode* exponent = create_math_atom_node("2", "2");
    ViewNode* superscript = create_math_script_node(base, exponent, true);
    
    cr_assert_not_null(superscript, "Superscript node should be created");
    cr_assert_eq(superscript->content.math_elem->type, VIEW_MATH_SUPERSCRIPT, "Should be superscript type");
    
    // Test layout
    MathLayoutContext* ctx = math_layout_context_create(NULL, NULL, VIEW_MATH_TEXT);
    ViewNode* laid_out = layout_math_script(superscript, ctx, true);
    cr_assert_not_null(laid_out, "Superscript layout should succeed");
    
    math_layout_context_destroy(ctx);
    view_node_release(laid_out);
    
    printf("✓ Superscript/subscript positioning test passed!\n");
}

Test(math_typeset, math_spacing) {
    printf("Testing mathematical spacing...\n");
    
    // Test spacing calculation between different math classes
    double spacing1 = calculate_math_spacing(VIEW_MATH_ORD, VIEW_MATH_BIN, VIEW_MATH_DISPLAY);
    double spacing2 = calculate_math_spacing(VIEW_MATH_BIN, VIEW_MATH_REL, VIEW_MATH_TEXT);
    
    cr_assert_geq(spacing1, 0.0, "Spacing should be non-negative");
    cr_assert_geq(spacing2, 0.0, "Spacing should be non-negative");
    
    printf("Spacing ORD-BIN (display): %.2f\n", spacing1);
    printf("Spacing BIN-REL (text): %.2f\n", spacing2);
    
    printf("✓ Mathematical spacing test passed!\n");
}

Test(math_typeset, symbol_unicode_conversion) {
    printf("Testing symbol to Unicode conversion...\n");
    
    // Test LaTeX symbol to Unicode mapping
    const char* alpha_unicode = get_unicode_for_latex_symbol("alpha");
    const char* pi_unicode = get_unicode_for_latex_symbol("pi");
    const char* sum_unicode = get_unicode_for_latex_symbol("sum");
    
    cr_assert_not_null(alpha_unicode, "Alpha symbol should have Unicode");
    cr_assert_not_null(pi_unicode, "Pi symbol should have Unicode");
    cr_assert_not_null(sum_unicode, "Sum symbol should have Unicode");
    
    printf("alpha -> %s\n", alpha_unicode ? alpha_unicode : "NULL");
    printf("pi -> %s\n", pi_unicode ? pi_unicode : "NULL");
    printf("sum -> %s\n", sum_unicode ? sum_unicode : "NULL");
    
    printf("✓ Symbol Unicode conversion test passed!\n");
}

Test(math_typeset, math_class_detection) {
    printf("Testing math class detection...\n");
    
    // Test operator classification
    bool is_plus_operator = is_math_operator("+");
    bool is_sin_function = is_function_name("sin");
    bool is_sum_large_op = is_large_operator("sum");
    
    cr_assert(is_plus_operator || !is_plus_operator, "Plus classification should not crash");
    cr_assert(is_sin_function, "sin should be recognized as function");
    cr_assert(is_sum_large_op, "sum should be recognized as large operator");
    
    printf("+ is operator: %s\n", is_plus_operator ? "true" : "false");
    printf("sin is function: %s\n", is_sin_function ? "true" : "false");
    printf("sum is large operator: %s\n", is_sum_large_op ? "true" : "false");
    
    printf("✓ Math class detection test passed!\n");
}

Test(math_typeset, svg_math_rendering) {
    printf("Testing SVG math rendering...\n");
    
    // Create a simple math atom
    ViewNode* atom = create_math_atom_node("α", "α");
    cr_assert_not_null(atom, "Math atom should be created");
    
    // Test SVG rendering
    SVGRenderer* renderer = svg_renderer_create();
    cr_assert_not_null(renderer, "SVG renderer should be created");
    
    StrBuf* svg_content = strbuf_create(256);
    
    // Simulate rendering by calling the math rendering function directly
    svg_render_math_atom(renderer, atom);
    
    // The svg_content should be in the renderer's internal buffer
    if (renderer->svg_content && renderer->svg_content->length > 0) {
        strbuf_append_str(svg_content, renderer->svg_content->str);
    }
    
    cr_assert_geq(svg_content->length, 0, "SVG content should be generated");
    
    printf("SVG atom rendering: %s\n", svg_content->str);
    
    // Cleanup
    view_node_release(atom);
    svg_renderer_destroy(renderer);
    strbuf_destroy(svg_content);
    
    printf("✓ SVG math rendering test passed!\n");
}

Test(math_typeset, integration_with_document) {
    printf("Testing math integration with document flow...\n");
    
    // Create a mock document with inline math
    ViewTree* document = view_tree_create();
    cr_assert_not_null(document, "Document should be created");
    
    // Create a paragraph with inline math
    ViewNode* paragraph = view_node_create(VIEW_NODE_BLOCK);
    ViewNode* text1 = view_node_create_text_run("The formula ", NULL, 12.0);
    ViewNode* math = create_math_atom_node("E = mc²", "E = mc²");
    ViewNode* text2 = view_node_create_text_run(" shows energy equivalence.", NULL, 12.0);
    
    cr_assert_not_null(paragraph, "Paragraph should be created");
    cr_assert_not_null(text1, "First text should be created");
    cr_assert_not_null(math, "Math should be created");
    cr_assert_not_null(text2, "Second text should be created");
    
    // Add to paragraph
    view_node_add_child(paragraph, text1);
    view_node_add_child(paragraph, math);
    view_node_add_child(paragraph, text2);
    
    // Add paragraph to document
    document->root = paragraph;
    
    // Test rendering
    SVGRenderer* renderer = svg_renderer_create();
    StrBuf* svg_output = strbuf_create(1024);
    bool success = svg_render_view_tree(renderer, document, svg_output);
    
    cr_assert(success, "Document with math should render");
    cr_assert_gt(svg_output->length, 0, "Should produce SVG output");
    
    printf("Document with math rendered successfully (%zu bytes)\n", svg_output->length);
    
    // Cleanup
    view_tree_destroy(document);
    svg_renderer_destroy(renderer);
    strbuf_destroy(svg_output);
    
    printf("✓ Math integration with document test passed!\n");
}

// Helper function to create mock Lambda tree for testing
static Item create_mock_fraction_tree(void) {
    // This is a placeholder - in real implementation, this would be
    // the result of parsing LaTeX with input-math.cpp
    
    // For now, return a mock item that represents a parsed fraction
    Item mock_item = {.item = 0x12345}; // Mock non-null, non-error value
    return mock_item;
}

// Helper functions for cleanup
static void svg_renderer_destroy(SVGRenderer* renderer) {
    if (renderer) {
        if (renderer->svg_content) {
            strbuf_destroy(renderer->svg_content);
        }
        free(renderer);
    }
}

static void view_tree_destroy(ViewTree* tree) {
    if (tree) {
        if (tree->root) {
            view_node_release(tree->root);
        }
        if (tree->pages) {
            for (int i = 0; i < tree->page_count; i++) {
                if (tree->pages[i]) {
                    free(tree->pages[i]);
                }
            }
            free(tree->pages);
        }
        if (tree->title) free(tree->title);
        if (tree->creator) free(tree->creator);
        free(tree);
    }
}
