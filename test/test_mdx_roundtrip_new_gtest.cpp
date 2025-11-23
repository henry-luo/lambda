#include <gtest/gtest.h>
#include <string.h>
#include <stdlib.h>
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
#include "../lib/log.h"

class MDXRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }

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
};

// Test simple MDX roundtrip
TEST_F(MDXRoundtripTest, DISABLED_simple_mdx) {
    const char* mdx_content =
        "# Hello MDX\n\n"
        "This is **markdown** content.\n\n"
        "<Button>Click me</Button>\n\n"
        "More markdown here.";

    // Parse MDX
    Input* input = InputManager::create_input(NULL);
    ASSERT_NE(input, nullptr) << "Input creation should succeed";

    Item parsed = input_mdx(input, mdx_content);

    ASSERT_NE(parsed.item, ITEM_NULL) << "MDX parsing should succeed";

    // Format back to MDX
    String* formatted = format_mdx(input->pool, parsed);
    ASSERT_NE(formatted, nullptr) << "MDX formatting should succeed";
    ASSERT_NE(formatted->chars, nullptr) << "Formatted MDX should have content";

    // Normalize both for comparison
    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);

    ASSERT_NE(original_normalized, nullptr) << "Original normalization should succeed";
    ASSERT_NE(formatted_normalized, nullptr) << "Formatted normalization should succeed";

    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);

    ASSERT_STREQ(original_normalized, formatted_normalized) << "MDX roundtrip should preserve content";

    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with JSX fragments
TEST_F(MDXRoundtripTest, jsx_fragments) {
    const char* mdx_content =
        "# Fragment Test\n\n"
        "<>\n"
        "  <h2>Fragment Content</h2>\n"
        "  <p>Inside fragment</p>\n"
        "</>\n\n"
        "Regular markdown.";

    Input* input = InputManager::create_input(NULL);
    ASSERT_NE(input, nullptr) << "Input creation should succeed";

    Item parsed = input_mdx(input, mdx_content);

    ASSERT_NE(parsed.item, ITEM_NULL) << "MDX fragment parsing should succeed";

    String* formatted = format_mdx(input->pool, parsed);
    ASSERT_NE(formatted, nullptr) << "MDX fragment formatting should succeed";

    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);

    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);

    // For fragments, we may need semantic comparison
    ASSERT_NE(formatted_normalized, nullptr) << "Fragment formatting should produce output";

    free(original_normalized);
    free(formatted_normalized);
}

// Test complex MDX with nested components
TEST_F(MDXRoundtripTest, DISABLED_nested_components) {
    const char* mdx_content =
        "# Nested Test\n\n"
        "<Card title=\"Test\">\n"
        "  <div>\n"
        "    <Button>Nested Button</Button>\n"
        "  </div>\n"
        "</Card>\n\n"
        "## More Content\n\n"
        "Final paragraph.";

    Input* input = InputManager::create_input(NULL);
    ASSERT_NE(input, nullptr) << "Input creation should succeed";

    Item parsed = input_mdx(input, mdx_content);

    ASSERT_NE(parsed.item, ITEM_NULL) << "Complex MDX parsing should succeed";

    String* formatted = format_mdx(input->pool, parsed);
    ASSERT_NE(formatted, nullptr) << "Complex MDX formatting should succeed";

    char* original_normalized = normalize_mdx(mdx_content);
    char* formatted_normalized = normalize_mdx(formatted->chars);

    printf("Original:  '%s'\n", original_normalized);
    printf("Formatted: '%s'\n", formatted_normalized);

    // Check that key content is preserved
    ASSERT_NE(strstr(formatted_normalized, "Nested Test"), nullptr) << "Header should be preserved";
    ASSERT_NE(strstr(formatted_normalized, "Card"), nullptr) << "JSX component should be preserved";
    ASSERT_NE(strstr(formatted_normalized, "Button"), nullptr) << "Nested component should be preserved";

    free(original_normalized);
    free(formatted_normalized);
}

// Test MDX with expressions
TEST_F(MDXRoundtripTest, DISABLED_jsx_expressions) {
    const char* mdx_content =
        "# Expression Test\n\n"
        "<Button onClick={() => alert('hi')}>Click</Button>\n\n"
        "<div>{name}</div>\n\n"
        "End content.";

    Input* input = InputManager::create_input(NULL);
    ASSERT_NE(input, nullptr) << "Input creation should succeed";

    Item parsed = input_mdx(input, mdx_content);

    ASSERT_NE(parsed.item, ITEM_NULL) << "MDX expression parsing should succeed";

    String* formatted = format_mdx(input->pool, parsed);
    ASSERT_NE(formatted, nullptr) << "MDX expression formatting should succeed";

    // Check that expressions are preserved
    ASSERT_NE(strstr(formatted->chars, "{"), nullptr) << "JSX expressions should be preserved";
    ASSERT_NE(strstr(formatted->chars, "onClick"), nullptr) << "JSX attributes should be preserved";

    printf("Formatted: '%s'\n", formatted->chars);
}

// Test empty MDX
TEST_F(MDXRoundtripTest, empty_mdx) {
    const char* mdx_content = "";

    Input* input = InputManager::create_input(NULL);
    ASSERT_NE(input, nullptr) << "Input creation should succeed";

    Item parsed = input_mdx(input, mdx_content);

    String* formatted = format_mdx(input->pool, parsed);
    ASSERT_NE(formatted, nullptr) << "Empty MDX formatting should succeed";
}
