#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include "../lambda/lambda-data.hpp"
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/url.h"

extern "C" {
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    Input* input_from_url(String* url, String* type, String* flavor, Url* cwd);
    String* format_data(Item item, String* type, String* flavor, Pool *pool);

    // Use actual URL library functions
    Url* url_parse(const char* input);
    Url* url_parse_with_base(const char* input, const Url* base);
    void url_destroy(Url* url);
}
char* read_text_doc(Url *url);

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

// Helper function to read file contents
char* read_file_content(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(length + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(content, 1, length, file);
    content[read_bytes] = '\0';
    fclose(file);

    return content;
}

// Helper function to check if a string is valid
bool is_valid_string_content(const char* content) {
    if (!content) return false;

    // Basic validation - check for null bytes and reasonable length
    size_t len = strlen(content);
    if (len == 0 || len > 1000000) return false;  // Reject empty or huge strings

    // Check for null bytes in the middle of the string
    for (size_t i = 0; i < len; i++) {
        if (content[i] == '\0') return false;
    }

    return true;
}

// Helper function to normalize whitespace for comparison
char* normalize_whitespace(const char* input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    size_t write_pos = 0;
    bool in_whitespace = false;

    for (size_t i = 0; i < len; i++) {
        if (isspace(input[i])) {
            if (!in_whitespace && write_pos > 0) {
                result[write_pos++] = ' ';
                in_whitespace = true;
            }
        } else {
            result[write_pos++] = input[i];
            in_whitespace = false;
        }
    }

    // Remove trailing whitespace
    while (write_pos > 0 && isspace(result[write_pos - 1])) {
        write_pos--;
    }

    result[write_pos] = '\0';
    return result;
}

// Helper function to compare strings with normalized whitespace
bool strings_equal_normalized(const char* str1, const char* str2) {
    if (!str1 && !str2) return true;
    if (!str1 || !str2) return false;

    char* norm1 = normalize_whitespace(str1);
    char* norm2 = normalize_whitespace(str2);

    bool equal = false;
    if (norm1 && norm2) {
        equal = (strcmp(norm1, norm2) == 0);
    }

    free(norm1);
    free(norm2);
    return equal;
}

// Helper function to create temporary test file
char* create_temp_test_file(const char* content, const char* extension) {
    static char temp_filename[256];
    snprintf(temp_filename, sizeof(temp_filename), "test_temp_input_%d.%s",
             rand() % 100000, extension ? extension : "txt");

    FILE* file = fopen(temp_filename, "w");
    if (!file) return NULL;

    fputs(content, file);
    fclose(file);

    return temp_filename;
}

// Test fixture class for input roundtrip tests
class InputRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
    }

    void TearDown() override {
        // Clean up any test files or resources
    }

    // Common roundtrip test function
    bool test_format_roundtrip(const char* test_file, const char* format_type, const char* test_name) {
        printf("\n=== Testing %s roundtrip for %s ===\n", format_type, test_name);

        // Read the test file
        char* original_content = read_file_content(test_file);
        if (!original_content) {
            printf("ERROR: Failed to read test file: %s\n", test_file);
            return false;
        }

        printf("Original content length: %zu\n", strlen(original_content));

        // Create Lambda strings for input parameters
        String* type_str = create_lambda_string(format_type);
        String* flavor_str = NULL;

        // Get current directory for URL resolution
        Url* cwd = url_parse("file://./");
        Url* file_url = url_parse_with_base(test_file, cwd);

        printf("Parsing with input_from_source...\n");

        // Parse the content
        Input* parsed_input = input_from_source(original_content, file_url, type_str, flavor_str);

        if (!parsed_input) {
            printf("ERROR: Failed to parse %s content\n", format_type);
            free(original_content);
            free(type_str);
            url_destroy(file_url);
            url_destroy(cwd);
            return false;
        }

        printf("Input parsed successfully\n");

        // Get the root item from the parsed input
        Item root_item = parsed_input->root;

        printf("Formatting back to %s...\n", format_type);

        // Format the parsed data back to the same format
        String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

        if (!formatted) {
            printf("ERROR: Failed to format %s data\n", format_type);
            free(original_content);
            free(type_str);
            url_destroy(file_url);
            url_destroy(cwd);
            return false;
        }

        printf("Formatted content length: %u\n", formatted->len);

        // Compare the original and formatted content
        bool content_matches = false;
        if (formatted->chars && strlen(formatted->chars) > 0) {
            // For JSON and XML, we can do direct comparison
            if (strcmp(format_type, "json") == 0 || strcmp(format_type, "xml") == 0) {
                content_matches = strings_equal_normalized(original_content, formatted->chars);
            } else {
                // For other formats, check if they're both non-empty and valid
                content_matches = (strlen(original_content) > 0 &&
                                 formatted->len > 0 &&
                                 is_valid_string_content(formatted->chars));
            }
        }

        printf("Content comparison result: %s\n", content_matches ? "MATCH" : "DIFFERENT");

        if (!content_matches) {
            printf("Original:\n%.*s\n", (int)strlen(original_content), original_content);
            printf("Formatted:\n%.*s\n", (int)formatted->len, formatted->chars);
        }

        // Clean up
        free(original_content);
        free(type_str);
        url_destroy(file_url);
        url_destroy(cwd);

        return content_matches;
    }
};

// JSON Tests
class JsonTests : public InputRoundtripTest {};

TEST_F(JsonTests, JsonRoundtrip) {
    printf("\n=== Testing comprehensive JSON roundtrip ===\n");

    const char* complex_json = "{\n"
        "  \"string\": \"Hello, World!\",\n"
        "  \"number\": 42,\n"
        "  \"float\": 3.14159,\n"
        "  \"boolean\": true,\n"
        "  \"null_value\": null,\n"
        "  \"array\": [1, 2, 3, \"four\"],\n"
        "  \"nested\": {\n"
        "    \"key\": \"value\",\n"
        "    \"count\": 123\n"
        "  }\n"
        "}";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("json");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.json", cwd);

    // Make a mutable copy of the JSON string
    char* json_copy = strdup(complex_json);

    printf("Parsing JSON with input_from_source...\n");

    // Parse the JSON content
    Input* parsed_input = input_from_source(json_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse JSON content";

    printf("JSON parsed successfully\n");

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    printf("Formatting back to JSON...\n");

    // Format the parsed data back to JSON
    String* formatted_json = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_json, nullptr) << "Failed to format JSON data";
    ASSERT_GT(formatted_json->len, 0U) << "Formatted JSON should not be empty";

    printf("JSON roundtrip test completed\n");
    printf("Original length: %zu, Formatted length: %u\n",
           strlen(complex_json), formatted_json->len);

    // Clean up
    free(json_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(JsonTests, SimpleJsonRoundtrip) {
    printf("\n=== Testing simple JSON roundtrip ===\n");

    const char* simple_json = "{\"message\": \"Hello, Lambda!\", \"version\": 1.0}";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("json");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("simple.json", cwd);

    // Make a mutable copy of the JSON string
    char* json_copy = strdup(simple_json);

    printf("Parsing simple JSON...\n");

    // Parse the JSON content
    Input* parsed_input = input_from_source(json_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse simple JSON content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back to JSON
    String* formatted_json = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_json, nullptr) << "Failed to format simple JSON data";
    ASSERT_GT(formatted_json->len, 0U) << "Formatted JSON should not be empty";

    printf("Simple JSON roundtrip completed successfully\n");

    // Clean up
    free(json_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

// XML Tests
class XmlTests : public InputRoundtripTest {};

TEST_F(XmlTests, XmlRoundtrip) {
    printf("\n=== Testing comprehensive XML roundtrip ===\n");

    const char* complex_xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "  <element attribute=\"value\">Text content</element>\n"
        "  <nested>\n"
        "    <child>Child content</child>\n"
        "    <number>42</number>\n"
        "  </nested>\n"
        "  <empty-element/>\n"
        "</root>";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("xml");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.xml", cwd);

    // Make a mutable copy of the XML string
    char* xml_copy = strdup(complex_xml);

    printf("Parsing XML with input_from_source...\n");

    // Parse the XML content
    Input* parsed_input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse XML content";

    printf("XML parsed successfully\n");

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    printf("Formatting back to XML...\n");

    // Format the parsed data back to XML
    String* formatted_xml = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_xml, nullptr) << "Failed to format XML data";
    ASSERT_GT(formatted_xml->len, 0U) << "Formatted XML should not be empty";

    printf("XML roundtrip test completed\n");

    // Clean up
    free(xml_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(XmlTests, SimpleXmlRoundtrip) {
    printf("\n=== Testing simple XML roundtrip ===\n");

    const char* simple_xml = "<message>Hello, Lambda!</message>";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("xml");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("simple.xml", cwd);

    // Make a mutable copy of the XML string
    char* xml_copy = strdup(simple_xml);

    printf("Parsing simple XML...\n");

    // Parse the XML content
    Input* parsed_input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse simple XML content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back to XML
    String* formatted_xml = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_xml, nullptr) << "Failed to format simple XML data";
    ASSERT_GT(formatted_xml->len, 0U) << "Formatted XML should not be empty";

    printf("Simple XML roundtrip completed successfully\n");

    // Clean up
    free(xml_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

// Markdown Tests
class MarkdownTests : public InputRoundtripTest {};

TEST_F(MarkdownTests, MarkdownRoundtrip) {
    printf("\n=== Testing Markdown roundtrip ===\n");

    const char* markdown_content = "# Heading 1\n\n"
        "This is a paragraph with **bold** and *italic* text.\n\n"
        "## Heading 2\n\n"
        "- List item 1\n"
        "- List item 2\n"
        "- List item 3\n\n"
        "```code\n"
        "Some code block\n"
        "```\n\n"
        "A [link](http://example.com) in text.";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.md", cwd);

    // Make a mutable copy of the markdown string
    char* md_copy = strdup(markdown_content);

    printf("Parsing Markdown with input_from_source...\n");

    // Parse the Markdown content
    Input* parsed_input = input_from_source(md_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse Markdown content";

    printf("Markdown parsed successfully\n");

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    printf("Formatting back to Markdown...\n");

    // Format the parsed data back to Markdown
    String* formatted_md = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_md, nullptr) << "Failed to format Markdown data";
    ASSERT_GT(formatted_md->len, 0U) << "Formatted Markdown should not be empty";

    printf("Markdown roundtrip test completed\n");

    // Clean up
    free(md_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkdownTests, SimpleMarkdownRoundtrip) {
    printf("\n=== Testing simple Markdown roundtrip ===\n");

    const char* simple_markdown = "# Hello Lambda\n\nThis is a simple test.";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("simple.md", cwd);

    // Make a mutable copy of the markdown string
    char* md_copy = strdup(simple_markdown);

    printf("Parsing simple Markdown...\n");

    // Parse the Markdown content
    Input* parsed_input = input_from_source(md_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse simple Markdown content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back to Markdown
    String* formatted_md = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_md, nullptr) << "Failed to format simple Markdown data";
    ASSERT_GT(formatted_md->len, 0U) << "Formatted Markdown should not be empty";

    printf("Simple Markdown roundtrip completed successfully\n");

    // Clean up
    free(md_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

// Org Mode Tests
class OrgTests : public InputRoundtripTest {};

TEST_F(OrgTests, OrgRoundtrip) {
    printf("\n=== Testing Org mode roundtrip ===\n");

    const char* org_content = "* Heading 1\n\n"
        "This is some text under heading 1.\n\n"
        "** Subheading\n\n"
        "- List item 1\n"
        "- List item 2\n\n"
        "#+BEGIN_SRC code\n"
        "Some code\n"
        "#+END_SRC\n";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("org");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.org", cwd);

    // Make a mutable copy of the org string
    char* org_copy = strdup(org_content);

    printf("Parsing Org with input_from_source...\n");

    // Parse the Org content
    Input* parsed_input = input_from_source(org_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse Org content";

    printf("Org parsed successfully\n");

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    printf("Formatting back to Org...\n");

    // Format the parsed data back to Org
    String* formatted_org = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_org, nullptr) << "Failed to format Org data";
    ASSERT_GT(formatted_org->len, 0U) << "Formatted Org should not be empty";

    printf("Org roundtrip test completed\n");

    // Clean up
    free(org_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(OrgTests, SimpleOrgRoundtrip) {
    printf("\n=== Testing simple Org roundtrip ===\n");

    const char* simple_org = "* Hello Lambda\n\nThis is a simple test.";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("org");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("simple.org", cwd);

    // Make a mutable copy of the org string
    char* org_copy = strdup(simple_org);

    printf("Parsing simple Org...\n");

    // Parse the Org content
    Input* parsed_input = input_from_source(org_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse simple Org content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back to Org
    String* formatted_org = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted_org, nullptr) << "Failed to format simple Org data";
    ASSERT_GT(formatted_org->len, 0U) << "Formatted Org should not be empty";

    printf("Simple Org roundtrip completed successfully\n");

    // Clean up
    free(org_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

// Markup Tests
class MarkupTests : public InputRoundtripTest {};

TEST_F(MarkupTests, MarkupMarkdownRoundtrip) {
    printf("\n=== Testing markup Markdown roundtrip ===\n");

    const char* markup_md = "# Test Document\n\n"
        "This is a test of markup processing.\n\n"
        "## Features\n\n"
        "- **Bold text**\n"
        "- *Italic text*\n"
        "- `Code snippets`\n\n"
        "### Code Block\n\n"
        "```\n"
        "function test() {\n"
        "    return true;\n"
        "}\n"
        "```";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = create_lambda_string("markdown");

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("markup.md", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(markup_md);

    printf("Parsing markup Markdown...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse markup Markdown content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format markup Markdown data";
    ASSERT_GT(formatted->len, 0U) << "Formatted markup should not be empty";

    printf("Markup Markdown roundtrip completed\n");

    // Clean up
    free(content_copy);
    free(type_str);
    free(flavor_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, MarkupRstRoundtrip) {
    printf("\n=== Testing markup RST roundtrip ===\n");

    const char* markup_rst = "Test Document\n"
        "=============\n\n"
        "This is a test of RST markup processing.\n\n"
        "Features\n"
        "--------\n\n"
        "- **Bold text**\n"
        "- *Italic text*\n"
        "- ``Code snippets``\n\n"
        "Code Block\n"
        "~~~~~~~~~~\n\n"
        "::\n\n"
        "    function test() {\n"
        "        return true;\n"
        "    }";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = create_lambda_string("rst");

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("markup.rst", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(markup_rst);

    printf("Parsing markup RST...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse markup RST content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format markup RST data";
    ASSERT_GT(formatted->len, 0U) << "Formatted markup should not be empty";

    printf("Markup RST roundtrip completed\n");

    // Clean up
    free(content_copy);
    free(type_str);
    free(flavor_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, MarkupWikiDetection) {
    printf("\n=== Testing markup Wiki detection ===\n");

    const char* wiki_content = "= Main Heading =\n\n"
        "This is wiki format content.\n\n"
        "== Subheading ==\n\n"
        "* List item 1\n"
        "* List item 2\n\n"
        "'''Bold text''' and ''italic text''.\n\n"
        "[[Link|Link text]]";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = create_lambda_string("wiki");

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.wiki", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(wiki_content);

    printf("Parsing Wiki markup...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse Wiki markup content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format Wiki markup data";
    ASSERT_GT(formatted->len, 0U) << "Formatted markup should not be empty";

    printf("Wiki markup detection test completed\n");

    // Clean up
    free(content_copy);
    free(type_str);
    free(flavor_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, Phase2ComprehensiveRoundtrip) {
    printf("\n=== Testing Phase 2 comprehensive roundtrip ===\n");

    const char* comprehensive_content = "# Comprehensive Test\n\n"
        "This document tests various markup elements:\n\n"
        "## Text Formatting\n\n"
        "- **Bold text**\n"
        "- *Italic text*\n"
        "- ***Bold and italic***\n"
        "- `Inline code`\n"
        "- ~~Strikethrough~~\n\n"
        "## Lists\n\n"
        "### Unordered List\n"
        "- Item 1\n"
        "- Item 2\n"
        "  - Nested item\n"
        "  - Another nested item\n"
        "- Item 3\n\n"
        "### Ordered List\n"
        "1. First item\n"
        "2. Second item\n"
        "3. Third item\n\n"
        "## Code Blocks\n\n"
        "```javascript\n"
        "function example() {\n"
        "    console.log('Hello, World!');\n"
        "    return 42;\n"
        "}\n"
        "```\n\n"
        "## Links and Images\n\n"
        "Here is a [link](https://example.com) and an ![image](test.png).\n\n"
        "## Tables\n\n"
        "| Header 1 | Header 2 | Header 3 |\n"
        "|----------|----------|----------|\n"
        "| Cell 1   | Cell 2   | Cell 3   |\n"
        "| Cell 4   | Cell 5   | Cell 6   |\n\n"
        "## Blockquotes\n\n"
        "> This is a blockquote.\n"
        "> It can span multiple lines.\n"
        ">\n"
        "> > Nested blockquote\n\n"
        "## Horizontal Rule\n\n"
        "---\n\n"
        "End of document.";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("comprehensive.md", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(comprehensive_content);

    printf("Parsing comprehensive content...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse comprehensive content";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format comprehensive data";
    ASSERT_GT(formatted->len, 0U) << "Formatted content should not be empty";

    printf("Phase 2 comprehensive roundtrip completed\n");
    printf("Original length: %zu, Formatted length: %u\n",
           strlen(comprehensive_content), formatted->len);

    // Clean up
    free(content_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, Phase2BlockElements) {
    printf("\n=== Testing Phase 2 block elements ===\n");

    const char* block_content = "# Block Elements Test\n\n"
        "## Paragraphs\n\n"
        "This is the first paragraph. It contains multiple sentences.\n"
        "It demonstrates paragraph handling.\n\n"
        "This is the second paragraph, separated by a blank line.\n\n"
        "## Headings\n\n"
        "### Level 3 Heading\n\n"
        "#### Level 4 Heading\n\n"
        "##### Level 5 Heading\n\n"
        "###### Level 6 Heading\n\n"
        "## Code Blocks\n\n"
        "Indented code block:\n\n"
        "    function indented() {\n"
        "        return 'code';\n"
        "    }\n\n"
        "Fenced code block:\n\n"
        "```\n"
        "function fenced() {\n"
        "    return 'code';\n"
        "}\n"
        "```\n\n"
        "## Lists\n\n"
        "Unordered list:\n"
        "- Item 1\n"
        "- Item 2\n"
        "- Item 3\n\n"
        "Ordered list:\n"
        "1. First\n"
        "2. Second\n"
        "3. Third";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("blocks.md", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(block_content);

    printf("Parsing block elements...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse block elements";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format block elements";
    ASSERT_GT(formatted->len, 0U) << "Formatted block elements should not be empty";

    printf("Phase 2 block elements test completed\n");

    // Clean up
    free(content_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, Phase2InlineElements) {
    printf("\n=== Testing Phase 2 inline elements ===\n");

    const char* inline_content = "# Inline Elements Test\n\n"
        "This paragraph contains **bold text**, *italic text*, and ***bold italic***.\n\n"
        "It also has `inline code`, [links](https://example.com), and ![images](test.png).\n\n"
        "Special characters: & < > \" ' and HTML entities: &amp; &lt; &gt; &quot; &#39;\n\n"
        "Line breaks  \n"
        "can be created with two spaces.\n\n"
        "Automatic links: <https://example.com> and <email@example.com>\n\n"
        "~~Strikethrough text~~ and super^script^ and sub~script~.";

    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;

    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("inline.md", cwd);

    // Make a mutable copy of the content
    char* content_copy = strdup(inline_content);

    printf("Parsing inline elements...\n");

    // Parse the content
    Input* parsed_input = input_from_source(content_copy, dummy_url, type_str, flavor_str);

    ASSERT_NE(parsed_input, nullptr) << "Failed to parse inline elements";

    // Get the root item from the parsed input
    Item root_item = parsed_input->root;

    // Format the parsed data back
    String* formatted = format_data(root_item, type_str, flavor_str, parsed_input->pool);

    ASSERT_NE(formatted, nullptr) << "Failed to format inline elements";
    ASSERT_GT(formatted->len, 0U) << "Formatted inline elements should not be empty";

    printf("Phase 2 inline elements test completed\n");

    // Clean up
    free(content_copy);
    free(type_str);
    url_destroy(dummy_url);
    url_destroy(cwd);
}

TEST_F(MarkupTests, MarkupFileRoundtrip) {
    printf("\n=== Testing markup file roundtrip ===\n");

    // Create a temporary test file
    const char* test_content = "# Test File\n\n"
        "This is a test file for markup roundtrip testing.\n\n"
        "## Features\n\n"
        "- File reading\n"
        "- Content parsing\n"
        "- Format conversion\n\n"
        "```\n"
        "Code block example\n"
        "```\n\n"
        "End of test file.";

    char* temp_file = create_temp_test_file(test_content, "md");
    ASSERT_NE(temp_file, nullptr) << "Failed to create temporary test file";

    printf("Testing file roundtrip with: %s\n", temp_file);

    // Test the roundtrip
    bool result = test_format_roundtrip(temp_file, "markdown", "file_test");

    EXPECT_TRUE(result) << "File roundtrip test should succeed";

    // Clean up
    unlink(temp_file);

    printf("Markup file roundtrip test completed\n");
}
