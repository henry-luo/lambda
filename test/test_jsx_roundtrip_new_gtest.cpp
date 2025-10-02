#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lambda/input/input.h"
#include "../lambda/format/format.h"
#include "../lib/string.h"
#include "../lib/mem-pool/include/mem_pool.h"

extern "C" {
    Input* input_from_source(const char* source, Url* url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    Url* parse_url(Url* base, const char* url_str);
    Url* get_current_dir(void);
}

class JSXRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }

    // Helper function to read file content
    static char* read_file_content(const char* filepath) {
        FILE* file = fopen(filepath, "r");
        if (!file) {
            printf("Failed to open file: %s\n", filepath);
            return NULL;
        }

        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        fseek(file, 0, SEEK_SET);

        char* content = (char*)malloc(length + 1);
        if (!content) {
            fclose(file);
            return NULL;
        }

        fread(content, 1, length, file);
        content[length] = '\0';
        fclose(file);

        return content;
    }

    // Helper function to normalize JSX for comparison
    static char* normalize_jsx(const char* jsx) {
        if (!jsx) return NULL;

        size_t len = strlen(jsx);
        char* normalized = (char*)malloc(len + 1);
        if (!normalized) return NULL;

        size_t write_pos = 0;
        bool in_tag = false;

        for (size_t i = 0; i < len; i++) {
            char c = jsx[i];

            if (c == '<') {
                in_tag = true;
                normalized[write_pos++] = c;
            } else if (c == '>') {
                in_tag = false;
                normalized[write_pos++] = c;
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                // Skip all whitespace between tags (outside of tags)
                if (!in_tag) {
                    // Look ahead to see if next non-whitespace is '<'
                    size_t j = i;
                    while (j < len && (jsx[j] == ' ' || jsx[j] == '\t' || jsx[j] == '\n' || jsx[j] == '\r')) {
                        j++;
                    }
                    if (j < len && jsx[j] == '<') {
                        // Skip whitespace between tags
                        i = j - 1; // Will be incremented by loop
                        continue;
                    }
                }
                // Preserve single space within tags or text content
                if (write_pos > 0 && normalized[write_pos - 1] != ' ') {
                    normalized[write_pos++] = ' ';
                }
            } else {
                normalized[write_pos++] = c;
            }
        }

        // Remove trailing whitespace
        while (write_pos > 0 && normalized[write_pos - 1] == ' ') {
            write_pos--;
        }

        normalized[write_pos] = '\0';
        return normalized;
    }

    // Helper function to perform JSX roundtrip test
    void test_jsx_roundtrip_file(const char* filename) {
        printf("Testing JSX roundtrip for: %s\n", filename);

        char filepath[256];
        snprintf(filepath, sizeof(filepath), "test/input/%s", filename);

        // Read original JSX content
        char* original_content = read_file_content(filepath);
        ASSERT_NE(original_content, nullptr) << "Failed to read JSX file: " << filepath;

        // Parse JSX
        Url* cwd = get_current_dir();
        Url* url = parse_url(cwd, filepath);
        ASSERT_NE(url, nullptr) << "Failed to parse URL";

        // Create a simple JSX type string structure
        static String jsx_type_struct = {4, 1, {'j', 's', 'x', '\0'}};
        String* jsx_type = &jsx_type_struct;

        Input* input = input_from_source(original_content, url, jsx_type, NULL);
        ASSERT_NE(input, nullptr) << "Failed to create input from JSX source";
        ASSERT_NE(input->root.item, ITEM_NULL) << "JSX parsing failed - no root element";

        // Format back to JSX
        String* formatted = format_data(input->root, jsx_type, NULL, input->pool);
        ASSERT_NE(formatted, nullptr) << "JSX formatting failed";
        ASSERT_NE(formatted->chars, nullptr) << "Formatted JSX is null";

        printf("Original: %s\n", original_content);
        printf("Formatted: %s\n", formatted->chars);

        // Normalize both for comparison
        char* normalized_original = normalize_jsx(original_content);
        char* normalized_formatted = normalize_jsx(formatted->chars);

        ASSERT_NE(normalized_original, nullptr) << "Failed to normalize original JSX";
        ASSERT_NE(normalized_formatted, nullptr) << "Failed to normalize formatted JSX";

        printf("Normalized original: %s\n", normalized_original);
        printf("Normalized formatted: %s\n", normalized_formatted);

        // Compare normalized versions
        ASSERT_STREQ(normalized_original, normalized_formatted) << "JSX roundtrip failed for " << filename;

        // Cleanup
        free(original_content);
        free(normalized_original);
        free(normalized_formatted);

        printf("JSX roundtrip test passed for: %s\n", filename);
    }
};

// Test cases for different JSX files
TEST_F(JSXRoundtripTest, simple_element) {
    test_jsx_roundtrip_file("simple.jsx");
}

TEST_F(JSXRoundtripTest, component_with_props) {
    test_jsx_roundtrip_file("component.jsx");
}

TEST_F(JSXRoundtripTest, jsx_fragment) {
    test_jsx_roundtrip_file("fragment.jsx");
}

TEST_F(JSXRoundtripTest, nested_elements) {
    test_jsx_roundtrip_file("nested.jsx");
}

TEST_F(JSXRoundtripTest, self_closing_tags) {
    test_jsx_roundtrip_file("self_closing.jsx");
}

// Test JSX expressions parsing
TEST_F(JSXRoundtripTest, jsx_expressions) {
    const char* jsx_with_expressions = "<div>{name} is {age} years old</div>";

    Url* cwd = get_current_dir();
    Url* url = parse_url(cwd, "test.jsx");
    // Create a simple JSX type string structure
    static String jsx_type_struct = {4, 1, {'j', 's', 'x', '\0'}};
    String* jsx_type = &jsx_type_struct;

    Input* input = input_from_source(jsx_with_expressions, url, jsx_type, NULL);

    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);

    // Format back
    String* formatted = format_data(input->root, jsx_type, NULL, input->pool);
    ASSERT_NE(formatted, nullptr);

    printf("JSX with expressions - Original: %s\n", jsx_with_expressions);
    printf("JSX with expressions - Formatted: %s\n", formatted->chars);
}

// Test JSX attributes
TEST_F(JSXRoundtripTest, jsx_attributes) {
    const char* jsx_with_attrs = "<button className=\"btn\" onClick={handleClick} disabled>Click</button>";

    Url* cwd = get_current_dir();
    Url* url = parse_url(cwd, "test.jsx");
    // Create a simple JSX type string structure
    static String jsx_type_struct = {4, 1, {'j', 's', 'x', '\0'}};
    String* jsx_type = &jsx_type_struct;

    Input* input = input_from_source(jsx_with_attrs, url, jsx_type, NULL);

    ASSERT_NE(input, nullptr);
    ASSERT_NE(input->root.item, ITEM_NULL);

    // Format back
    String* formatted = format_data(input->root, jsx_type, NULL, input->pool);
    ASSERT_NE(formatted, nullptr);

    printf("JSX with attributes - Original: %s\n", jsx_with_attrs);
    printf("JSX with attributes - Formatted: %s\n", formatted->chars);
}
