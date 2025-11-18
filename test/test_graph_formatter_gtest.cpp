#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-graph.h"
#include "../lambda/format/format.h"
#include "../lib/arraylist.h"
#include "../lib/mempool.h"
#include "../lib/url.h"

extern "C" {
    Input* input_from_source(const char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, Pool* pool);
    void pool_destroy(Pool* pool);
    void arraylist_free(ArrayList* list);
}

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    // Allocate String struct + space for the null-terminated string
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;

    result->len = len;
    result->ref_cnt = 1;
    // Copy the string content to the chars array at the end of the struct
    strcpy(result->chars, text);

    return result;
}

// Helper function to free Lambda String
void free_lambda_string(String* str) {
    if (str) {
        free(str);
    }
}

// Test fixture for graph formatter tests
class GraphFormatterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test environment
    }

    void TearDown() override {
        // Clean up test environment
    }
};

// Test DOT graph formatting
TEST_F(GraphFormatterTest, FormatBasicDOTGraph) {
    const char* dot_content =
        "digraph test {\n"
        "    A -> B [label=\"edge1\"];\n"
        "    B -> C;\n"
        "}";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("dot");

    // Parse the graph
    Input* input = input_from_source(dot_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format back to DOT
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("dot");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result contains key DOT elements
    EXPECT_NE(strstr(result->chars, "digraph"), nullptr);
    EXPECT_NE(strstr(result->chars, "->"), nullptr);

    printf("DOT formatted result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, don't destroy pool
    // InputManager will clean up when program exits
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test Mermaid graph formatting
TEST_F(GraphFormatterTest, FormatBasicMermaidGraph) {
    const char* mermaid_content =
        "flowchart TD\n"
        "    A[Start] --> B[End]\n"
        "    B --> C[Final]\n";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("mermaid");

    // Parse the graph
    Input* input = input_from_source(mermaid_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format back to Mermaid
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("mermaid");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result contains key Mermaid elements
    EXPECT_NE(strstr(result->chars, "flowchart"), nullptr);
    EXPECT_NE(strstr(result->chars, "-->"), nullptr);

    printf("Mermaid formatted result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test D2 graph formatting
TEST_F(GraphFormatterTest, FormatBasicD2Graph) {
    const char* d2_content =
        "x -> y\n"
        "a -> b: \"Connection Label\"\n";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("d2");

    // Parse the graph
    Input* input = input_from_source(d2_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format back to D2
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("d2");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result contains key D2 elements
    EXPECT_NE(strstr(result->chars, "->"), nullptr);

    printf("D2 formatted result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test cross-format conversion: DOT to Mermaid
TEST_F(GraphFormatterTest, ConvertDOTtoMermaid) {
    const char* dot_content =
        "digraph example {\n"
        "    A -> B;\n"
        "    B -> C [label=\"test\"];\n"
        "}";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("dot");

    // Parse as DOT
    Input* input = input_from_source(dot_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format as Mermaid
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("mermaid");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result is Mermaid format
    EXPECT_NE(strstr(result->chars, "flowchart"), nullptr);
    EXPECT_NE(strstr(result->chars, "-->"), nullptr);

    printf("DOT to Mermaid conversion result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test cross-format conversion: Mermaid to D2
TEST_F(GraphFormatterTest, ConvertMermaidToD2) {
    const char* mermaid_content =
        "flowchart LR\n"
        "    Start --> Process\n"
        "    Process --> End\n";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("mermaid");

    // Parse as Mermaid
    Input* input = input_from_source(mermaid_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format as D2
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("d2");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result is D2 format (no flowchart header)
    EXPECT_EQ(strstr(result->chars, "flowchart"), nullptr);
    EXPECT_NE(strstr(result->chars, "->"), nullptr);

    printf("Mermaid to D2 conversion result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test cross-format conversion: D2 to DOT
TEST_F(GraphFormatterTest, ConvertD2toDOT) {
    const char* d2_content =
        "server: {\n"
        "  shape: rectangle\n"
        "}\n"
        "client -> server: \"API call\"\n";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("d2");

    // Parse as D2
    Input* input = input_from_source(d2_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format as DOT
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("dot");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result is DOT format
    EXPECT_NE(strstr(result->chars, "digraph"), nullptr);
    EXPECT_NE(strstr(result->chars, "->"), nullptr);

    printf("D2 to DOT conversion result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test complex graph with nodes, edges, and attributes
TEST_F(GraphFormatterTest, FormatComplexGraphToDOT) {
    const char* d2_content =
        "# Complex graph with multiple features\n"
        "database: {\n"
        "  shape: cylinder\n"
        "  style: {\n"
        "    fill: lightblue\n"
        "    stroke: darkblue\n"
        "  }\n"
        "}\n"
        "\n"
        "api: {\n"
        "  shape: rectangle\n"
        "  style.fill: orange\n"
        "}\n"
        "\n"
        "users -> api: \"HTTP Request\"\n"
        "api -> database: \"SQL Query\"\n"
        "database -> api: \"Results\"\n"
        "api -> users: \"Response\"\n";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("d2");

    // Parse as D2
    Input* input = input_from_source(d2_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->root.type_id(), LMD_TYPE_ELEMENT);

    // Format as DOT
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("dot");

    String* result = format_data(input->root, format_type_str, format_flavor_str, input->pool);
    ASSERT_NE(result, nullptr);

    // Verify the result contains expected elements
    EXPECT_NE(strstr(result->chars, "digraph"), nullptr);
    EXPECT_NE(strstr(result->chars, "->"), nullptr);

    printf("Complex graph DOT formatting result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test error handling with invalid input
TEST_F(GraphFormatterTest, HandleInvalidInput) {
    String* format_type_str = create_lambda_string("graph");
    String* format_flavor_str = create_lambda_string("dot");

    // Try to format a null item
    Item invalid_item = {.item = ITEM_NULL};

    String* result = format_data(invalid_item, format_type_str, format_flavor_str, nullptr);
    // Should handle gracefully (might return null or empty string)

    // Clean up
    free_lambda_string(format_type_str);
    free_lambda_string(format_flavor_str);
}

// Test default flavor handling
TEST_F(GraphFormatterTest, DefaultFlavorHandling) {
    const char* d2_content = "a -> b";

    String* input_type_str = create_lambda_string("graph");
    String* input_flavor_str = create_lambda_string("d2");

    // Parse as D2
    Input* input = input_from_source(d2_content, NULL, input_type_str, input_flavor_str);
    ASSERT_NE(input, nullptr);

    // Format with no flavor (should default to DOT)
    String* format_type_str = create_lambda_string("graph");

    String* result = format_data(input->root, format_type_str, nullptr, input->pool);
    ASSERT_NE(result, nullptr);

    // Should default to DOT format
    EXPECT_NE(strstr(result->chars, "digraph"), nullptr);

    printf("Default flavor result:\n%s\n", result->chars);

    // Clean up
    // Note: input is managed by InputManager singleton, cleanup handled automatically
    free_lambda_string(input_type_str);
    free_lambda_string(input_flavor_str);
    free_lambda_string(format_type_str);
}
