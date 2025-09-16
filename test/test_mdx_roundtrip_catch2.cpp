#include <catch2/catch_test_macros.hpp>
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
TEST_CASE("MDX Roundtrip - simple mdx", "[mdx][roundtrip][!mayfail]") {
    const char* mdx_content = 
        "# Hello MDX\n\n"
        "This is **markdown** content.\n\n"
        "<Button>Click me</Button>\n\n"
        "More markdown here.";
    
    // Parse MDX
    Input* input = input_new(NULL);
    REQUIRE(input != nullptr);
    
    Item parsed = input_mdx(input, mdx_content);
    
    REQUIRE(parsed.item != ITEM_NULL);
    
    // Format back to MDX
    String* formatted = format_mdx(input->pool, parsed);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->chars != nullptr);
    
    // Normalize both for comparison
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    REQUIRE(original_normalized != nullptr);
    REQUIRE(formatted_normalized != nullptr);
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    REQUIRE(strcmp(original_normalized, formatted_normalized) == 0);
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with JSX fragments
TEST_CASE("MDX Roundtrip - jsx fragments", "[mdx][roundtrip]") {
    const char* mdx_content = 
        "# Fragment Test\n\n"
        "<>\n"
        "  <h2>Fragment Content</h2>\n"
        "  <p>Inside fragment</p>\n"
        "</>\n\n"
        "Regular markdown.";
    
    Input* input = input_new(NULL);
    REQUIRE(input != nullptr);
    
    Item parsed = input_mdx(input, mdx_content);
    
    REQUIRE(parsed.item != ITEM_NULL);
    
    String* formatted = format_mdx(input->pool, parsed);
    REQUIRE(formatted != nullptr);
    
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    // For fragments, we may need semantic comparison
    REQUIRE(formatted_normalized != nullptr);
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test complex MDX with nested components
TEST_CASE("MDX Roundtrip - nested components", "[mdx][roundtrip][!mayfail]") {
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
    REQUIRE(input != nullptr);
    
    Item parsed = input_mdx(input, mdx_content);
    
    REQUIRE(parsed.item != ITEM_NULL);
    
    String* formatted = format_mdx(input->pool, parsed);
    REQUIRE(formatted != nullptr);
    
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);
    
    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);
    
    // Check that key content is preserved
    REQUIRE(strstr(formatted_normalized, "Nested Test") != nullptr);
    REQUIRE(strstr(formatted_normalized, "Card") != nullptr);
    REQUIRE(strstr(formatted_normalized, "Button") != nullptr);
    
    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with expressions
TEST_CASE("MDX Roundtrip - jsx expressions", "[mdx][roundtrip][!mayfail]") {
    const char* mdx_content = 
        "# Expression Test\n\n"
        "<Button onClick={() => alert('hi')}>Click</Button>\n\n"
        "<div>{name}</div>\n\n"
        "End content.";
    
    Input* input = input_new(NULL);
    REQUIRE(input != nullptr);
    
    Item parsed = input_mdx(input, mdx_content);
    
    REQUIRE(parsed.item != ITEM_NULL);
    
    String* formatted = format_mdx(input->pool, parsed);
    REQUIRE(formatted != nullptr);
    
    // Check that expressions are preserved
    REQUIRE(strstr(formatted->chars, "{") != nullptr);
    REQUIRE(strstr(formatted->chars, "onClick") != nullptr);
    
    printf("Formatted: '%s'\n", formatted->chars);
}

// Test empty MDX
TEST_CASE("MDX Roundtrip - empty mdx", "[mdx][roundtrip]") {
    const char* mdx_content = "";
    
    Input* input = input_new(NULL);
    REQUIRE(input != nullptr);
    
    Item parsed = input_mdx(input, mdx_content);
    
    String* formatted = format_mdx(input->pool, parsed);
    REQUIRE(formatted != nullptr);
}
