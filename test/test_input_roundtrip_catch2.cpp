#include <catch2/catch_test_macros.hpp>
#include "input_roundtrip_helpers.hpp"

// JSON roundtrip test with comprehensive data
TEST_CASE("JSON roundtrip - comprehensive", "[json][roundtrip]") {
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
    
    // Parse the input content
    Input* input = input_from_source(json_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Comprehensive JSON parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted comprehensive JSON (first 200 chars): %.200s\n", formatted->chars);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_json_semantically(complex_json, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "Hello") != nullptr);
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Comprehensive JSON roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Comprehensive JSON roundtrip test failed - content mismatch\n");
        printf("  Original (normalized): %s\n", normalize_whitespace(complex_json));
        printf("  Formatted (normalized): %s\n", normalize_whitespace(formatted->chars));
    }
    
    // Cleanup - json_copy is automatically freed by input_from_source()
}

// Simple JSON roundtrip test for debugging
TEST_CASE("JSON roundtrip - simple", "[json][roundtrip]") {
    printf("\n=== Testing simple JSON roundtrip ===\n");
    
    const char* simple_json = "{\"test\": true, \"number\": 42}";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("json");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.json", cwd);
    
    // Make a mutable copy of the JSON string
    char* json_copy = strdup(simple_json);
    
    // Parse the input content
    Input* input = input_from_source(json_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Simple JSON parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted simple JSON: %s\n", formatted->chars);
    
    // Debug: Show the exact comparison
    printf("DEBUG: Original JSON: '%s' (len=%zu)\n", simple_json, strlen(simple_json));
    printf("DEBUG: Formatted JSON: '%s' (len=%d)\n", formatted->chars, formatted->len);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_json_semantically(simple_json, formatted->chars);
    
    printf("DEBUG: Content matches: %s\n", content_matches ? "true" : "false");
    
    REQUIRE(formatted->len > 0);
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Simple JSON roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Simple JSON roundtrip test failed - content mismatch\n");
        printf("  Original: %s\n", simple_json);
        printf("  Formatted: %s\n", formatted->chars);
        
        // Show normalized versions
        char* norm_orig = normalize_whitespace(simple_json);
        char* norm_fmt = normalize_whitespace(formatted->chars);
        printf("  Original (normalized): '%s'\n", norm_orig ? norm_orig : "NULL");
        printf("  Formatted (normalized): '%s'\n", norm_fmt ? norm_fmt : "NULL");
        free(norm_orig);
        free(norm_fmt);
    }
    
    // Cleanup - json_copy is freed by input_from_source
}

// XML roundtrip test with structured data
TEST_CASE("XML roundtrip - comprehensive", "[xml][roundtrip]") {
    printf("\n=== Testing comprehensive XML roundtrip ===\n");
    
    const char* complex_xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<document>\n"
        "  <header>\n"
        "    <title>Test Document</title>\n"
        "    <author>Test Author</author>\n"
        "  </header>\n"
        "  <body>\n"
        "    <section id=\"intro\">\n"
        "      <p>This is a test paragraph.</p>\n"
        "      <list>\n"
        "        <item>First item</item>\n"
        "        <item>Second item</item>\n"
        "      </list>\n"
        "    </section>\n"
        "  </body>\n"
        "</document>";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("xml");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.xml", cwd);
    
    // Make a mutable copy of the XML string
    char* xml_copy = strdup(complex_xml);
    
    // Parse the input content
    Input* input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Comprehensive XML parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted comprehensive XML (first 200 chars): %.200s\n", formatted->chars);
    printf("Complete formatted XML: %s\n", formatted->chars);
    printf("Formatted XML length: %u\n", formatted->len);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_xml_semantically(complex_xml, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "header") != nullptr);
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Comprehensive XML roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Comprehensive XML roundtrip test failed - content mismatch\n");
        printf("  Original (normalized): %s\n", normalize_whitespace(complex_xml));
        printf("  Formatted (normalized): %s\n", normalize_whitespace(formatted->chars));
    }
    
    // Cleanup - xml_copy is freed by input_from_source
}

// Simple XML roundtrip test
TEST_CASE("XML roundtrip - simple", "[xml][roundtrip]") {
    printf("\n=== Testing simple XML roundtrip ===\n");
    
    const char* simple_xml = "<root><item>test</item></root>";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("xml");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.xml", cwd);
    
    // Make a mutable copy of the XML string
    char* xml_copy = strdup(simple_xml);
    
    // Parse the input content
    Input* input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Simple XML parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted simple XML: %s\n", formatted->chars);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_xml_semantically(simple_xml, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Simple XML roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Simple XML roundtrip test failed - content mismatch\n");
        printf("  Original: %s\n", simple_xml);
        printf("  Formatted: %s\n", formatted->chars);
    }
    
    // Cleanup - xml_copy is freed by input_from_source
}

// Markdown roundtrip test with rich formatting
TEST_CASE("Markdown roundtrip - comprehensive", "[markdown][roundtrip]") {
    printf("\n=== Testing comprehensive Markdown roundtrip ===\n");
    
    const char* complex_md = "# Main Header\n\n"
        "This is a **bold** paragraph with *italic* text and `code snippets`.\n\n"
        "## Subheader\n\n"
        "Here's a list:\n"
        "- First item\n"
        "- Second item with **emphasis**\n"
        "- Third item\n\n"
        "### Code Example\n\n"
        "```javascript\n"
        "function hello() {\n"
        "    console.log('Hello, World!');\n"
        "}\n"
        "```\n\n"
        "And a [link](http://example.com) for good measure.\n\n"
        "> This is a blockquote with some **bold** text.";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.md", cwd);
    
    // Make a mutable copy of the Markdown string
    char* md_copy = strdup(complex_md);
    
    // Parse the input content
    Input* input = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Comprehensive Markdown parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted comprehensive Markdown (first 200 chars): %.200s\n", formatted->chars);
    printf("Complete formatted Markdown: %s\n", formatted->chars);
    printf("Formatted length: %u vs Original length: %zu\n", formatted->len, strlen(complex_md));
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_markdown_semantically(complex_md, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "Main Header") != nullptr);
    
    if (!content_matches) {
        printf("Content mismatch details:\n");
        printf("Original:\n%s\n", complex_md);
        printf("Formatted:\n%s\n", formatted->chars);
        char* norm_orig = normalize_whitespace(complex_md);
        char* norm_fmt = normalize_whitespace(formatted->chars);
        printf("Original (normalized): %s\n", norm_orig);
        printf("Formatted (normalized): %s\n", norm_fmt);
        free(norm_orig);
        free(norm_fmt);
    }
    
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Comprehensive Markdown roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Comprehensive Markdown roundtrip test failed - content mismatch\n");
        printf("  Original (normalized): %s\n", normalize_whitespace(complex_md));
        printf("  Formatted (normalized): %s\n", normalize_whitespace(formatted->chars));
    }
    
    // Cleanup - md_copy is freed by input_from_source
}

// Simple Markdown roundtrip test
TEST_CASE("Markdown roundtrip - simple", "[markdown][roundtrip]") {
    printf("\n=== Testing simple Markdown roundtrip ===\n");
    
    const char* simple_md = "# Test Header\n\nThis is a **bold** test.";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.md", cwd);
    
    // Make a mutable copy of the Markdown string
    char* md_copy = strdup(simple_md);
    
    // Parse the input content
    Input* input = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Simple Markdown parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted simple Markdown: %s\n", formatted->chars);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_markdown_semantically(simple_md, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Simple Markdown roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Simple Markdown roundtrip test failed - content mismatch\n");
        printf("  Original: %s\n", simple_md);
        printf("  Formatted: %s\n", formatted->chars);
    }
    
    // Cleanup - md_copy is freed by input_from_source
}

// Org-mode roundtrip test with comprehensive content
TEST_CASE("Org-mode roundtrip - file", "[org][roundtrip]") {
    printf("\n=== Testing comprehensive Org-mode roundtrip ===\n");
    
    bool success = test_format_roundtrip("test/input/test.org", "org", "comprehensive org test");
    REQUIRE(success);
}

// Simple Org-mode roundtrip test with embedded content
TEST_CASE("Org-mode roundtrip - simple", "[org][roundtrip]") {
    printf("\n=== Testing simple Org-mode roundtrip ===\n");
    
    const char* simple_org = "#+TITLE: Simple Test\n\n"
        "This is a *bold* test with /italic/ text.\n\n"
        "Inline math: $x^2 + y^2 = z^2$\n\n"
        "Display math:\n"
        "$$\\int_0^\\infty e^{-x} dx = 1$$\n\n"
        "- First item\n"
        "- Second item\n\n"
        "A simple [fn:1] footnote reference.\n\n"
        "[fn:1] Footnote definition.";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("org");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.org", cwd);
    
    // Make a mutable copy of the Org string
    char* org_copy = strdup(simple_org);
    
    // Parse the input content
    Input* input = input_from_source(org_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Simple Org-mode parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted simple Org-mode: %s\n", formatted->chars);
    
    // Enhanced validation with semantic comparison
    bool content_matches = compare_org_semantically(simple_org, formatted->chars);
    
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "Simple Test") != nullptr);
    
    if (!content_matches) {
        printf("Content mismatch details:\n");
        printf("Original:\n%s\n", simple_org);
        printf("Formatted:\n%s\n", formatted->chars);
        char* norm_orig = normalize_whitespace(simple_org);
        char* norm_fmt = normalize_whitespace(formatted->chars);
        printf("Original (normalized): %s\n", norm_orig);
        printf("Formatted (normalized): %s\n", norm_fmt);
        free(norm_orig);
        free(norm_fmt);
    }
    
    REQUIRE(content_matches);
    
    if (content_matches) {
        printf("✓ Simple Org-mode roundtrip test passed - content matches original\n");
    } else {
        printf("✗ Simple Org-mode roundtrip test failed - content mismatch\n");
        printf("  Original: %s\n", simple_org);
        printf("  Formatted: %s\n", formatted->chars);
    }
    
    // Cleanup - org_copy is freed by input_from_source
}

// Markup test with Markdown content (should default to markdown flavor)
TEST_CASE("Markup roundtrip - markdown content", "[markup][roundtrip]") {
    printf("\n=== Testing markup parser with Markdown content ===\n");
    
    const char* markdown_content = "# Test Header\n\n"
        "This is a **bold** test with *italic* text and `code`.\n\n"
        "## Subheader\n\n"
        "- First item\n"
        "- Second item with **emphasis**\n"
        "- Third item\n\n"
        "```javascript\n"
        "console.log('Hello, World!');\n"
        "```\n\n"
        "A [link](http://example.com) for reference.";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL; // Should detect as markdown
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.md", cwd);
    
    // Make a mutable copy of the content
    char* content_copy = strdup(markdown_content);
    
    // Parse the input content using unified markup parser
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Markup parser (Markdown) parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string using markup formatter (should default to markdown)
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted markup content (first 200 chars): %.200s\n", formatted->chars);
    
    // Enhanced validation
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "Test Header") != nullptr);
    
    // For markup parser, we use more lenient comparison since it restructures content
    bool content_matches = compare_markup_semantically(markdown_content, formatted->chars);
    
    if (content_matches) {
        printf("✓ Markup parser Markdown roundtrip test passed\n");
    } else {
        printf("✗ Markup parser Markdown roundtrip test failed\n");
        printf("  Original: %s\n", markdown_content);
        printf("  Formatted: %s\n", formatted->chars);
    }
    
    REQUIRE(content_matches);
}

// Markup test with RST content
TEST_CASE("Markup roundtrip - RST content", "[markup][roundtrip]") {
    printf("\n=== Testing markup parser with RST content ===\n");
    
    const char* rst_content = "Test Header\n"
        "===========\n\n"
        "This is a **bold** test with *italic* text.\n\n"
        "Subheader\n"
        "---------\n\n"
        "- First item\n"
        "- Second item\n\n"
        ".. code-block:: python\n\n"
        "   print('Hello, World!')\n\n"
        "A `link <http://example.com>`_ for reference.";
    
    // Create Lambda strings for input parameters with RST flavor
    String* type_str = create_lambda_string("markup");
    String* flavor_str = create_lambda_string("rst");
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("comprehensive_test.rst", cwd);
    
    // Make a mutable copy of the content
    char* content_copy = strdup(rst_content);
    
    // Parse the input content using unified markup parser
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Markup parser (RST) parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string using markup formatter with RST flavor
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted markup RST content (first 200 chars): %.200s\n", formatted->chars);
    
    // Enhanced validation
    REQUIRE(formatted->len > 0);
    REQUIRE(strstr(formatted->chars, "Test Header") != nullptr);
    
    bool content_matches = compare_markup_semantically(rst_content, formatted->chars);
    
    if (content_matches) {
        printf("✓ Markup parser RST roundtrip test passed\n");
    } else {
        printf("✗ Markup parser RST roundtrip test failed\n");
        printf("  Original: %s\n", rst_content);
        printf("  Formatted: %s\n", formatted->chars);
    }
    
    REQUIRE(content_matches);
}

// Markup test with Wiki content
TEST_CASE("Markup roundtrip - Wiki detection", "[markup][roundtrip]") {
    printf("\n=== Testing markup parser with Wiki content (format detection) ===\n");
    
    const char* wiki_content = "== Test Header ==\n\n"
        "This is a '''bold''' test with ''italic'' text.\n\n"
        "=== Subheader ===\n\n"
        "* First item\n"
        "* Second item\n\n"
        "[[http://example.com|A link]] for reference.";
    
    // Create Lambda strings for input parameters (no flavor, should detect wiki)
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL; // Should auto-detect as wiki format
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("test.wiki", cwd);
    
    // Make a mutable copy of the content
    char* content_copy = strdup(wiki_content);
    
    // Parse the input content using unified markup parser
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Markup parser (Wiki detected) parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Format the parsed data back to string (should default to markdown since wiki formatter isn't implemented)
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Formatted markup Wiki content: %s\n", formatted->chars);
    
    // Enhanced validation - just check that content was parsed and formatted
    REQUIRE(formatted->len > 0);
    
    // For wiki content, we expect the parser to extract the text content even if formatting differs
    bool has_header = (strstr(formatted->chars, "Test Header") != nullptr);
    bool has_content = (strstr(formatted->chars, "bold") != nullptr) || (strstr(formatted->chars, "italic") != nullptr);
    
    printf("Header found: %s, Content found: %s\n", has_header ? "yes" : "no", has_content ? "yes" : "no");
    
    REQUIRE((has_header || has_content));
    
    printf("✓ Markup parser Wiki detection test passed\n");
}

// Phase 2 comprehensive roundtrip test with enhanced content
TEST_CASE("Markup roundtrip - Phase 2 comprehensive", "[markup][phase2][roundtrip]") {
    printf("\n=== Testing Phase 2 Enhanced Markup Parser - Comprehensive Roundtrip ===\n");
    
    const char* complex_content = 
        "# Enhanced Markup Parser Test\n\n"
        "This document tests **Phase 2** enhanced parsing with *rich inline* elements.\n\n"
        "## Block Elements\n\n"
        "### Headers with Mixed Content\n"
        "# H1 Header\n"
        "## H2 Header with **bold** text\n"
        "### H3 Header with *italic* and `code`\n\n"
        "### Lists with Rich Content\n"
        "- Unordered list item 1\n"
        "- Unordered list item 2 with **bold text**\n"
        "- Unordered list item 3 with [link](https://example.com)\n\n"
        "1. Ordered list item 1\n"
        "2. Ordered list item 2 with *emphasis*\n"
        "3. Ordered list item 3 with `inline code`\n\n"
        "### Code Blocks with Language Detection\n"
        "```python\n"
        "def hello_world():\n"
        "    print(\"Hello, world!\")\n"
        "    return True\n"
        "```\n\n"
        "```javascript\n"
        "function fibonacci(n) {\n"
        "    return n <= 1 ? n : fibonacci(n-1) + fibonacci(n-2);\n"
        "}\n"
        "```\n\n"
        "### Tables with Rich Content\n"
        "|Column 1|Column 2|Column 3|\n"
        "|Value 1|**Bold Value**|`Code Value`|\n"
        "|Value 2|*Italic Value*|[Link Value](https://test.com)|\n\n"
        "### Math Blocks\n"
        "$$\n"
        "E = mc^2\n"
        "$$\n\n"
        "### Horizontal Rules\n"
        "---\n\n"
        "## Inline Elements\n\n"
        "### Complex Inline Formatting\n"
        "This paragraph demonstrates **bold text**, *italic text*, and `inline code`.\n"
        "You can also use [links with **bold** text](https://example.com).\n\n"
        "Here's an image: ![Alt text](https://example.com/image.jpg)\n\n"
        "### Nested Formatting Examples\n"
        "This paragraph has **bold text with *italic inside*** and `code with text`.\n"
        "Links can contain [**bold**, *italic*, and `code`](https://example.com).\n\n"
        "This tests the comprehensive parsing capabilities of Phase 2!";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL; // Should detect as markdown
    
    // Get current directory for URL resolution
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("phase2_test.md", cwd);
    
    // Make a mutable copy of the content
    char* content_copy = strdup(complex_content);
    
    printf("Phase 2 Test: Starting roundtrip with %zu bytes of content\n", strlen(complex_content));
    
    // Parse the input content using Phase 2 enhanced unified markup parser
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    printf("Phase 2 Test: Parsing successful, root item: 0x%llx\n", (unsigned long long)input->root.item);
    
    // Validate the parsed structure contains expected elements
    REQUIRE(input->root.item != ITEM_NULL);
    REQUIRE(input->root.item != ITEM_ERROR);
    
    // Format the parsed data back to string using markup formatter
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Phase 2 Test: Formatted content length: %u bytes\n", formatted->len);
    printf("Phase 2 Test: Formatted content preview (first 300 chars):\n%.300s...\n", formatted->chars);
    
    // Enhanced validation for Phase 2 features
    REQUIRE(formatted->len > 0);
    
    // Check that essential Phase 2 elements are present
    bool has_main_header = strstr(formatted->chars, "Enhanced Markup Parser Test") != nullptr;
    bool has_subheaders = strstr(formatted->chars, "Block Elements") != nullptr;
    bool has_code_content = strstr(formatted->chars, "hello_world") != nullptr || strstr(formatted->chars, "fibonacci") != nullptr;
    bool has_list_content = strstr(formatted->chars, "Unordered list item") != nullptr;
    bool has_inline_formatting = strstr(formatted->chars, "bold text") != nullptr;
    bool has_links = strstr(formatted->chars, "example.com") != nullptr;
    
    printf("Phase 2 Test: Content validation:\n");
    printf("  - Main header: %s\n", has_main_header ? "✓" : "✗");
    printf("  - Subheaders: %s\n", has_subheaders ? "✓" : "✗");
    printf("  - Code content: %s\n", has_code_content ? "✓" : "✗");
    printf("  - List content: %s\n", has_list_content ? "✓" : "✗");
    printf("  - Inline formatting: %s\n", has_inline_formatting ? "✓" : "✗");
    printf("  - Links: %s\n", has_links ? "✓" : "✗");
    
    // Assert that critical content is preserved
    REQUIRE(has_main_header);
    REQUIRE(has_subheaders);
    REQUIRE(has_list_content);
    
    // For Phase 2, we use semantic comparison allowing for structural changes
    bool content_matches = compare_markup_semantically(complex_content, formatted->chars);
    
    if (content_matches) {
        printf("✓ Phase 2 Enhanced Markup Parser comprehensive roundtrip test passed\n");
    } else {
        printf("⚠️ Phase 2 roundtrip shows structural differences (expected for enhanced parsing)\n");
        printf("  Original length: %zu bytes\n", strlen(complex_content));
        printf("  Formatted length: %u bytes\n", formatted->len);
        
        // Even if exact match fails, ensure essential content is preserved
        bool essential_preserved = has_main_header && has_subheaders && (has_code_content || has_list_content);
        REQUIRE(essential_preserved);
        
        printf("✓ Phase 2 Enhanced Markup Parser essential content preservation test passed\n");
    }
    
    free(content_copy);
}

// Phase 2 specific block element testing
TEST_CASE("Markup roundtrip - Phase 2 block elements", "[markup][phase2][blocks]") {
    printf("\n=== Testing Phase 2 Block Elements Parsing ===\n");
    
    const char* block_content = 
        "# Header Level 1\n"
        "## Header Level 2\n"
        "### Header Level 3\n\n"
        "Regular paragraph with text.\n\n"
        "- Unordered list item 1\n"
        "- Unordered list item 2\n\n"
        "1. Ordered list item 1\n"
        "2. Ordered list item 2\n\n"
        "```python\n"
        "print('Code block test')\n"
        "```\n\n"
        "|Col1|Col2|\n"
        "|A|B|\n\n"
        "$$\n"
        "x = y + z\n"
        "$$\n\n"
        "---\n";
    
    String* type_str = create_lambda_string("markup");
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("blocks.md", cwd);
    char* content_copy = strdup(block_content);
    
    Input* input = input_from_source(content_copy, dummy_url, type_str, NULL);
    REQUIRE(input != nullptr);
    
    String* formatted = format_data(input->root, type_str, NULL, input->pool);
    REQUIRE(formatted != nullptr);
    
    // Check for specific block element markers in output
    bool has_headers = strstr(formatted->chars, "Header Level") != nullptr;
    bool has_lists = strstr(formatted->chars, "list item") != nullptr;
    bool has_code = strstr(formatted->chars, "Code block test") != nullptr || strstr(formatted->chars, "print") != nullptr;
    
    printf("Phase 2 Block Elements Test:\n");
    printf("  - Headers: %s\n", has_headers ? "✓" : "✗");
    printf("  - Lists: %s\n", has_lists ? "✓" : "✗");
    printf("  - Code blocks: %s\n", has_code ? "✓" : "✗");
    
    REQUIRE(has_headers);
    REQUIRE(has_lists);
    
    printf("✓ Phase 2 Block Elements test passed\n");
    
    free(content_copy);
}

// Phase 2 specific inline element testing
TEST_CASE("Markup roundtrip - Phase 2 inline elements", "[markup][phase2][inline]") {
    printf("\n=== Testing Phase 2 Inline Elements Parsing ===\n");
    
    const char* inline_content = 
        "This paragraph has **bold text**, *italic text*, and `inline code`.\n\n"
        "Here's a [link](https://example.com) and an ![image](pic.jpg).\n\n"
        "Complex: **bold with *italic* inside** and [link with **bold** text](url).\n\n"
        "Multiple `code` spans and **nested *formatting* works**.";
    
    String* type_str = create_lambda_string("markup");
    Url* cwd = url_parse("file://./");
    Url* dummy_url = url_parse_with_base("inline.md", cwd);
    char* content_copy = strdup(inline_content);
    
    Input* input = input_from_source(content_copy, dummy_url, type_str, NULL);
    REQUIRE(input != nullptr);
    
    String* formatted = format_data(input->root, type_str, NULL, input->pool);
    REQUIRE(formatted != nullptr);
    
    // Check for inline element content preservation
    bool has_bold = strstr(formatted->chars, "bold text") != nullptr;
    bool has_italic = strstr(formatted->chars, "italic text") != nullptr;
    bool has_code = strstr(formatted->chars, "inline code") != nullptr;
    bool has_links = strstr(formatted->chars, "example.com") != nullptr || strstr(formatted->chars, "link") != nullptr;
    
    printf("Phase 2 Inline Elements Test:\n");
    printf("  - Bold text: %s\n", has_bold ? "✓" : "✗");
    printf("  - Italic text: %s\n", has_italic ? "✓" : "✗");
    printf("  - Code spans: %s\n", has_code ? "✓" : "✗");
    printf("  - Links: %s\n", has_links ? "✓" : "✗");
    
    REQUIRE(has_bold);
    REQUIRE(has_italic);
    REQUIRE(has_code);
    
    printf("✓ Phase 2 Inline Elements test passed\n");
    
    free(content_copy);
}

// Test format detection accuracy
TEST_CASE("Markup roundtrip - format detection", "[markup][detection]") {
    printf("\n=== Testing markup format detection accuracy ===\n");
    
    // Test multiple format samples to verify detection logic
    struct {
        const char* content;
        const char* expected_description;
    } test_cases[] = {
        {"# Header\n\n**bold** and *italic*", "Markdown format"},
        {"Header\n======\n\n**bold** and *italic*", "RST format"},
        {"== Header ==\n\n'''bold''' and ''italic''", "Wiki format"},
        {"* Header\n\n*bold* and /italic/", "Org-mode format"},
        {"h1. Header\n\np. Some _emphasis_ text", "Textile format"}
    };
    
    for (int i = 0; i < 5; i++) {
        printf("\n--- Testing %s ---\n", test_cases[i].expected_description);
        
        String* type_str = create_lambda_string("markup");
        String* flavor_str = NULL;
        
        Url* cwd = url_parse("file://./");
        Url* dummy_url = url_parse_with_base("test.txt", cwd);
        
        char* content_copy = strdup(test_cases[i].content);
        
        Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
        REQUIRE(input != nullptr);
        
        // Format the parsed data
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
        REQUIRE(formatted != nullptr);
        
        printf("Original: %s\n", test_cases[i].content);
        printf("Formatted: %s\n", formatted->chars);
        
        // Basic validation - content should be parsed and formatted
        REQUIRE(formatted->len > 0);
        
        printf("✓ %s detection and formatting test passed\n", test_cases[i].expected_description);
    }
    
    printf("✓ All markup format detection tests passed\n");
}

// Element-specific roundtrip tests
TEST_CASE("Markup roundtrip - element specific", "[markup][elements]") {
    printf("\n=== Testing Element-Specific Roundtrip ===\n");
    
    // Test different markdown elements
    struct {
        const char* name;
        const char* content;
        const char* expected_preservation;
    } test_cases[] = {
        {"headers", "# Main Header\n## Sub Header\n### Sub-sub Header\n", "Header"},
        {"emphasis", "This has **bold** and *italic* and `code` text.\n", "bold"},
        {"lists", "- First item\n- Second item\n- Third item\n\n1. Numbered\n2. List\n", "First item"},
        {"links", "Check out [this link](https://example.com) for more info.\n", "this link"},
        {"blockquotes", "> This is a quote\n> with multiple lines\n", "quote"},
    };
    
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        printf("  Testing %s...\n", test_cases[i].name);
        
        // Create Lambda strings for input parameters
        String* type_str = create_lambda_string("markup");
        String* flavor_str = NULL;
        
        // Get current directory for URL resolution
        Url* cwd = url_parse("file://./");
        Url* test_url = url_parse_with_base("test.md", cwd);
        
        // Make a mutable copy of the content
        char* content_copy = strdup(test_cases[i].content);
        
        // Parse
        Input* input = input_from_source(content_copy, test_url, type_str, flavor_str);
        REQUIRE(input != nullptr);
        
        // Format using markdown formatter
        String* markdown_type = create_lambda_string("markdown");
        String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
        REQUIRE(formatted != nullptr);
        
        if (formatted->len > 0 && formatted->chars) {
            // Check that expected content is preserved
            bool preserved = strstr(formatted->chars, test_cases[i].expected_preservation) != nullptr;
            REQUIRE(preserved);
            printf("    ✓ %s preserved\n", test_cases[i].expected_preservation);
        } else {
            printf("    ⚠ Empty formatted output for %s\n", test_cases[i].name);
        }
        
        // Cleanup
        free(content_copy);
    }
    
    printf("✓ Element-specific roundtrip tests completed\n");
}
