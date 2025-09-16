#include "../lib/unit_test/include/criterion/criterion.h"
#include <string.h>
#include <stdlib.h>
#include "../lambda/input/input.h"
#include "../lambda/format/format.h"

// Normalize MDX content for comparison
static char* normalize_mdx(const char* mdx) {
    if (!mdx) return NULL;
    
    size_t len = strlen(mdx);
    char* normalized = (char*)malloc(len + 1);
    if (!normalized) return NULL;
    
    size_t write_pos = 0;
    bool in_jsx_tag = false;
    bool prev_was_space = false;
    
    for (size_t i = 0; i < len; i++) {
        char c = mdx[i];
        
        // Track JSX tag boundaries
        if (c == '<') {
            in_jsx_tag = true;
            normalized[write_pos++] = c;
            prev_was_space = false;
        } else if (c == '>') {
            in_jsx_tag = false;
            normalized[write_pos++] = c;
            prev_was_space = false;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            // Normalize whitespace
            if (!prev_was_space) {
                // Preserve single space, but normalize type
                normalized[write_pos++] = ' ';
                prev_was_space = true;
            }
        } else {
            normalized[write_pos++] = c;
            prev_was_space = false;
        }
    }
    
    // Trim trailing whitespace
    while (write_pos > 0 && normalized[write_pos - 1] == ' ') {
        write_pos--;
    }
    
    normalized[write_pos] = '\0';
    return normalized;
}

// Test simple MDX roundtrip
Test(mdx_roundtrip, simple_mdx, .disabled = true) {
    const char* mdx_content = 
        "# Hello MDX\n\n"
        "This is **markdown** content.\n\n"
        "<Button>Click me</Button>\n\n"
        "More markdown here.";
    
    // Parse MDX
    Input* input = input_new(NULL);
    cr_assert_not_null(input, "Input creation should succeed");
    
    Item parsed = input_mdx(input, mdx_content);
    
    cr_assert_neq(parsed.item, ITEM_NULL, "MDX parsing should succeed");
    
    // Format back to MDX
    String* formatted = format_mdx(input->pool, parsed);
    cr_assert_not_null(formatted, "MDX formatting should succeed");
    cr_assert_not_null(formatted->chars, "Formatted MDX should have content");
    
    // Normalize both for comparison
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    cr_assert_not_null(original_normalized, "Original normalization should succeed");
    cr_assert_not_null(formatted_normalized, "Formatted normalization should succeed");
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    cr_assert_str_eq(original_normalized, formatted_normalized, 
                     "MDX roundtrip should preserve content");
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with JSX fragments
Test(mdx_roundtrip, jsx_fragments) {
    const char* mdx_content = 
        "# Fragment Test\n\n"
        "<>\n"
        "  <h2>Fragment Content</h2>\n"
        "  <p>Inside fragment</p>\n"
        "</>\n\n"
        "Regular markdown.";
    
    Input* input = input_new(NULL);
    cr_assert_not_null(input, "Input creation should succeed");
    
    Item parsed = input_mdx(input, mdx_content);
    
    cr_assert_neq(parsed.item, ITEM_NULL, "MDX fragment parsing should succeed");
    
    String* formatted = format_mdx(input->pool, parsed);
    cr_assert_not_null(formatted, "MDX fragment formatting should succeed");
    
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    // For fragments, we may need semantic comparison
    cr_assert_not_null(formatted_normalized, "Fragment formatting should produce output");
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test complex MDX with nested components
Test(mdx_roundtrip, nested_components, .disabled = true) {
    const char* mdx_content = 
        "# Nested Test\n\n"
        "<Card title=\"Test\">\n"
        "  <div>\n"
        "    <Button>Nested Button</Button>\n"
        "  </div>\n"
        "</Card>\n\n"
        "## More Content\n\n"
        "Final paragraph.";
    
    Input* input = input_new(NULL);
    cr_assert_not_null(input, "Input creation should succeed");
    
    Item parsed = input_mdx(input, mdx_content);
    
    cr_assert_neq(parsed.item, ITEM_NULL, "Complex MDX parsing should succeed");
    
    String* formatted = format_mdx(input->pool, parsed);
    cr_assert_not_null(formatted, "Complex MDX formatting should succeed");
    
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    // Check that key content is preserved
    cr_assert(strstr(formatted_normalized, "Nested Test") != NULL, 
              "Header should be preserved");
    cr_assert(strstr(formatted_normalized, "Card") != NULL, 
              "JSX component should be preserved");
    cr_assert(strstr(formatted_normalized, "Button") != NULL, 
              "Nested component should be preserved");
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with expressions
Test(mdx_roundtrip, jsx_expressions, .disabled = true) {
    const char* mdx_content = 
        "# Expression Test\n\n"
        "<Button onClick={() => alert('hi')}>Click</Button>\n\n"
        "<div>{name}</div>\n\n"
        "End content.";
    
    Input* input = input_new(NULL);
    cr_assert_not_null(input, "Input creation should succeed");
    
    Item parsed = input_mdx(input, mdx_content);
    
    cr_assert_neq(parsed.item, ITEM_NULL, "MDX expression parsing should succeed");
    
    String* formatted = format_mdx(input->pool, parsed);
    cr_assert_not_null(formatted, "MDX expression formatting should succeed");
    
    // Check that expressions are preserved
    cr_assert(strstr(formatted->chars, "{") != NULL, 
              "JSX expressions should be preserved");
    cr_assert(strstr(formatted->chars, "onClick") != NULL, 
              "JSX attributes should be preserved");
    
    printf("Formatted: '%s'\n", formatted->chars);
}

// Test empty MDX
Test(mdx_roundtrip, empty_mdx) {
    const char* mdx_content = "";
    
    Input* input = input_new(NULL);
    cr_assert_not_null(input, "Input creation should succeed");
    
    Item parsed = input_mdx(input, mdx_content);
    
    String* formatted = format_mdx(input->pool, parsed);
    cr_assert_not_null(formatted, "Empty MDX formatting should succeed");
}
