#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "../lambda/lambda.h"
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include <lexbor/url/url.h>

// Include the Input struct definition (matching lambda-data.hpp)
typedef struct Input {
    void* url;
    void* path;
    VariableMemPool* pool; // memory pool
    ArrayList* type_list;  // list of types
    Item root;
    StrBuf* sb;
} Input;

// Forward declarations
Input* input_from_source(char* source, lxb_url_t* abs_url, String* type, String* flavor);
Input* input_from_url(String* url, String* type, String* flavor, lxb_url_t* cwd);
String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
lxb_url_t* get_current_dir();
lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
char* read_text_doc(lxb_url_t *url);
void print_item(StrBuf *strbuf, Item item);

// Forward declarations for Lambda runtime 
extern __thread Context* context;

// Functions we need for setting up the context
void heap_init();
void frame_start();
void frame_end();
void heap_destroy();
num_stack_t* num_stack_create(size_t initial_capacity);
void num_stack_destroy(num_stack_t *stack);

Context* create_test_context();
void destroy_test_context(Context* ctx);

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
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    fclose(file);
    return content;
}

// Helper function to free Lambda string
// Lambda strings are managed by the memory pool - no need to free them manually

// Context management
static Context* test_ctx = NULL;

Context* create_test_context() {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize minimal context for testing
    ctx->num_stack = num_stack_create(16);
    ctx->ast_pool = NULL;
    ctx->consts = NULL;
    ctx->type_list = NULL;
    ctx->type_info = NULL;
    ctx->cwd = NULL;
    ctx->result = ITEM_NULL;
    ctx->heap = NULL;  // Will be initialized in setup
    
    return ctx;
}

void destroy_test_context(Context* ctx) {
    if (!ctx) return;
    if (ctx->num_stack) {
        num_stack_destroy((num_stack_t*)ctx->num_stack);
    }
    // free(ctx);
}

// Setup and teardown functions
void input_setup(void) {
    test_ctx = create_test_context();
    // Set the global context BEFORE calling heap_init
    context = test_ctx;
    
    // Now initialize heap and frame
    heap_init();
    frame_start();
    
    cr_assert_not_null(test_ctx, "Failed to create test context");
}

void input_teardown(void) {
    if (test_ctx && context) {
        frame_end();
        heap_destroy();
        
        if (test_ctx->num_stack) {
            num_stack_destroy((num_stack_t*)test_ctx->num_stack);
        }
        
        free(test_ctx);
        test_ctx = NULL;
        context = NULL;
    }
}

TestSuite(input_roundtrip_tests, .init = input_setup, .fini = input_teardown);

// Create separate test suites to avoid conflicts
TestSuite(json_tests, .init = input_setup, .fini = input_teardown);
TestSuite(xml_tests, .init = input_setup, .fini = input_teardown);
TestSuite(markdown_tests, .init = input_setup, .fini = input_teardown);

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
    String* flavor_str = NULL; // Use default flavor
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    if (!cwd) {
        printf("ERROR: Failed to get current directory\n");
        free(original_content);
        return false;
    }
    
    // Parse the URL for the test file
    lxb_url_t* file_url = parse_url(cwd, test_file);
    if (!file_url) {
        printf("ERROR: Failed to parse URL for test file\n");
        free(original_content);
        return false;
    }
    
    // Parse the input content
    Input* input = input_from_source(original_content, file_url, type_str, flavor_str);
    if (!input) {
        printf("ERROR: Failed to parse %s input\n", format_type);
        free(original_content);
        return false;
    }
    
    printf("Input parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    if (!formatted) {
        printf("ERROR: Failed to format %s data\n", format_type);
        free(original_content);
        return false;
    }
    
    printf("Formatted content length: %d\n", formatted->len);
    printf("Formatted content (first 200 chars): %.200s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty
    bool success = (formatted->len > 0);
    
    if (success) {
        printf("✓ %s roundtrip test passed for %s\n", format_type, test_name);
    } else {
        printf("✗ %s roundtrip test failed for %s\n", format_type, test_name);
    }
    
    // Cleanup
    free(original_content);
    // Note: Don't free type_str as it may be managed by the memory pool
    // Note: Don't free formatted string as it's managed by the memory pool
    
    return success;
}

// JSON roundtrip test with comprehensive data
Test(json_tests, json_roundtrip) {
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
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.json");
    
    // Make a mutable copy of the JSON string
    char* json_copy = strdup(complex_json);
    
    // Parse the input content
    Input* input = input_from_source(json_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse comprehensive JSON input");
    
    printf("Comprehensive JSON parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format comprehensive JSON data");
    
    printf("Formatted comprehensive JSON (first 200 chars): %.200s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty and contains key elements
    cr_assert(formatted->len > 0, "Formatted JSON should not be empty");
    cr_assert(strstr(formatted->chars, "Hello") != NULL, "Formatted JSON should contain string data");
    
    // Cleanup - json_copy is automatically freed by input_from_source()
}

// XML roundtrip test with structured data
Test(xml_tests, xml_roundtrip) {
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
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.xml");
    
    // Make a mutable copy of the XML string
    char* xml_copy = strdup(complex_xml);
    
    // Parse the input content
    Input* input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse comprehensive XML input");
    
    printf("Comprehensive XML parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format comprehensive XML data");
    
    printf("Formatted comprehensive XML (first 200 chars): %.200s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty and contains key elements
    cr_assert(formatted->len > 0, "Formatted XML should not be empty");
    cr_assert(strstr(formatted->chars, "document") != NULL, "Formatted XML should contain document structure");
    
    // Cleanup - xml_copy is freed by input_from_source
}

// Markdown roundtrip test with rich formatting
Test(markdown_tests, markdown_roundtrip) {
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
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.md");
    
    // Make a mutable copy of the Markdown string
    char* md_copy = strdup(complex_md);
    
    // Parse the input content
    Input* input = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse comprehensive Markdown input");
    
    printf("Comprehensive Markdown parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format comprehensive Markdown data");
    
    printf("Formatted comprehensive Markdown (first 200 chars): %.200s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty and contains key elements
    cr_assert(formatted->len > 0, "Formatted Markdown should not be empty");
    cr_assert(strstr(formatted->chars, "Main Header") != NULL, "Formatted Markdown should contain header");
    
    // Cleanup - md_copy is freed by input_from_source
}

// Additional test with smaller JSON for debugging
Test(json_tests, simple_json_roundtrip) {
    printf("\n=== Testing simple JSON roundtrip ===\n");
    
    const char* simple_json = "{\"test\": true, \"number\": 42}";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("json");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.json");
    
    // Make a mutable copy of the JSON string
    char* json_copy = strdup(simple_json);
    
    // Parse the input content
    Input* input = input_from_source(json_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse simple JSON input");
    
    printf("Simple JSON parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format simple JSON data");
    
    printf("Formatted simple JSON: %s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty
    cr_assert(formatted->len > 0, "Formatted JSON should not be empty");
    
    // Cleanup - json_copy is freed by input_from_source
    // Note: Don't free type_str as it may be managed by the memory pool
}

// Additional test with simple XML
Test(xml_tests, simple_xml_roundtrip) {
    printf("\n=== Testing simple XML roundtrip ===\n");
    
    const char* simple_xml = "<root><item>test</item></root>";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("xml");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.xml");
    
    // Make a mutable copy of the XML string
    char* xml_copy = strdup(simple_xml);
    
    // Parse the input content
    Input* input = input_from_source(xml_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse simple XML input");
    
    printf("Simple XML parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format simple XML data");
    
    printf("Formatted simple XML: %s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty
    cr_assert(formatted->len > 0, "Formatted XML should not be empty");
    
    // Cleanup - xml_copy is freed by input_from_source
    // Note: Don't free type_str as it may be managed by the memory pool
}

// Additional test with simple Markdown
Test(markdown_tests, simple_markdown_roundtrip) {
    printf("\n=== Testing simple Markdown roundtrip ===\n");
    
    const char* simple_md = "# Test Header\n\nThis is a **bold** test.";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.md");
    
    // Make a mutable copy of the Markdown string
    char* md_copy = strdup(simple_md);
    
    // Parse the input content
    Input* input = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse simple Markdown input");
    
    printf("Simple Markdown parsing successful, root item: 0x%llx\n", (unsigned long long)input->root);
    
    // Format the parsed data back to string
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format simple Markdown data");
    
    printf("Formatted simple Markdown: %s\n", formatted->chars);
    
    // Basic validation - check that formatted content is not empty
    cr_assert(formatted->len > 0, "Formatted Markdown should not be empty");
    
    // Cleanup - md_copy is freed by input_from_source
    // Note: Don't free type_str as it may be managed by the memory pool
}
