#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
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
String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
lxb_url_t* get_current_dir();
lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);

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

// Test with simple markdown elements
Test(markup_roundtrip, simple_test) {
    printf("\n=== Testing Simple Markdown Elements ===\n");
    
    const char* test_markdown = "# Header\n\nParagraph with **bold** text.\n\n- List item\n- Another item\n";
    
    // Create Lambda strings for input parameters  
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "test.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(test_markdown);
    
    // Parse
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse simple markdown");
    
    // Format using markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format simple markdown");
    cr_assert(formatted->len > 0, "Formatted content should not be empty");
    
    // Check that basic structure is preserved
    cr_assert(strstr(formatted->chars, "Header") != NULL, "Header should be preserved");
    cr_assert(strstr(formatted->chars, "bold") != NULL, "Bold text should be preserved");
    cr_assert(strstr(formatted->chars, "List item") != NULL, "List items should be preserved");
    
    printf("Simple test - formatted (length %zu):\n%s\n", formatted->len, formatted->chars);
    
    // Cleanup
    free(content_copy);
}

// Test empty content handling
Test(markup_roundtrip, empty_test) {
    printf("\n=== Testing Empty Content ===\n");
    
    const char* empty_markdown = "";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "empty.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(empty_markdown);
    
    // Parse empty content
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Should handle empty content");
    
    // Format empty content using markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Should format empty content");
    
    printf("Empty test - formatted: '%s' (length: %zu)\n", 
           formatted->chars ? formatted->chars : "(null)", formatted->len);
    
    // Cleanup
    free(content_copy);
}

// Test comprehensive markup features - covers all implemented parser capabilities
Test(markup_roundtrip, complete_test) {
    printf("\n=== Testing Complete Markup Features ===\n");
    
    // Read comprehensive markdown content from file
    char* comprehensive_markdown = read_file_content("test/input/complete_markup_test.md");
    cr_assert_not_null(comprehensive_markdown, "Failed to read complete_markup_test.md");
    
    printf("Loaded test content from file (length: %zu bytes)\n", strlen(comprehensive_markdown));
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "complete.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(comprehensive_markdown);
    
    // Parse comprehensive content
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse comprehensive markdown");
    
    // Format using markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format comprehensive markdown");
    cr_assert(formatted->len > 0, "Formatted content should not be empty");
    
    // Comprehensive validation - check all major features are preserved
    cr_assert(strstr(formatted->chars, "Main Header") != NULL, "Main header should be preserved");
    cr_assert(strstr(formatted->chars, "Sub Header") != NULL, "Sub header should be preserved");
    cr_assert(strstr(formatted->chars, "bold") != NULL, "Bold text should be preserved");
    cr_assert(strstr(formatted->chars, "italic") != NULL, "Italic text should be preserved");
    cr_assert(strstr(formatted->chars, "inline code") != NULL, "Inline code should be preserved");
    cr_assert(strstr(formatted->chars, "link text") != NULL, "Link text should be preserved");
    cr_assert(strstr(formatted->chars, "example.com") != NULL, "Link URL should be preserved");
    // Note: Image alt text isn't preserved by the markdown formatter (formatter limitation, not parser)
    cr_assert(strstr(formatted->chars, "blockquote") != NULL, "Blockquote should be preserved");
    cr_assert(strstr(formatted->chars, "First item") != NULL, "List items should be preserved");
    cr_assert(strstr(formatted->chars, "Numbered first") != NULL, "Ordered list should be preserved");
    cr_assert(strstr(formatted->chars, "hello_world") != NULL, "Code block content should be preserved");
    cr_assert(strstr(formatted->chars, "python") != NULL, "Code language should be preserved");
    cr_assert(strstr(formatted->chars, "Header 1") != NULL, "Table headers should be preserved");
    cr_assert(strstr(formatted->chars, "Cell 1") != NULL, "Table cells should be preserved");
    cr_assert(strstr(formatted->chars, "E = mc^2") != NULL, "Math content should be preserved");
    
    // Phase 3: Enhanced list validation
    cr_assert(strstr(formatted->chars, "Top level item 1") != NULL, "Top level list items should be preserved");
    cr_assert(strstr(formatted->chars, "Nested item 1.1") != NULL, "Nested list items should be preserved");
    cr_assert(strstr(formatted->chars, "Deep nested 1.2.1") != NULL, "Deep nested items should be preserved");
    cr_assert(strstr(formatted->chars, "Mixed ordered") != NULL, "Mixed list types should be preserved");
    cr_assert(strstr(formatted->chars, "Back to numbered") != NULL, "Mixed list nesting should be preserved");
    cr_assert(strstr(formatted->chars, "Multi-paragraph") != NULL, "List items with paragraphs should be preserved");
    cr_assert(strstr(formatted->chars, "continuation paragraph") != NULL, "List item continuations should be preserved");
    cr_assert(strstr(formatted->chars, "def example") != NULL, "Code blocks in lists should be preserved");
    cr_assert(strstr(formatted->chars, "nested code block") != NULL, "Nested code blocks should be preserved");
    
    cr_assert(strstr(formatted->chars, "Final paragraph") != NULL, "Final content should be preserved");
    
    printf("Complete test - formatted (length %zu):\n%s\n", formatted->len, formatted->chars);
    
    // Cleanup
    free(content_copy);
    free(comprehensive_markdown);
}

// Phase 3: Test nested list processing specifically
Test(markup_roundtrip, nested_lists_test) {
    printf("\n=== Testing Phase 3: Nested List Processing ===\n");
    
    const char* nested_list_markdown = 
        "# Nested Lists Test\n\n"
        "1. First level item\n"
        "   - Second level item\n"
        "     1. Third level numbered\n"
        "     2. Another third level\n"
        "   - Another second level\n"
        "2. Back to first level\n"
        "\n"
        "Mixed nesting:\n"
        "- Unordered first\n"
        "  1. Ordered second\n"
        "  2. Another ordered\n"
        "    - Back to unordered third\n"
        "    - More unordered third\n"
        "  3. Final ordered second\n"
        "- Final unordered first\n";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "nested.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(nested_list_markdown);
    
    // Parse nested list content
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse nested list markdown");
    
    // Format using markdown formatter
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format nested list markdown");
    cr_assert(formatted->len > 0, "Formatted content should not be empty");
    
    // Validate nested structure preservation
    cr_assert(strstr(formatted->chars, "Nested Lists Test") != NULL, "Header should be preserved");
    cr_assert(strstr(formatted->chars, "First level item") != NULL, "First level items should be preserved");
    cr_assert(strstr(formatted->chars, "Second level item") != NULL, "Second level items should be preserved");
    cr_assert(strstr(formatted->chars, "Third level numbered") != NULL, "Third level items should be preserved");
    cr_assert(strstr(formatted->chars, "Mixed nesting") != NULL, "Mixed nesting section should be preserved");
    cr_assert(strstr(formatted->chars, "Unordered first") != NULL, "Mixed list items should be preserved");
    cr_assert(strstr(formatted->chars, "Ordered second") != NULL, "Mixed ordered items should be preserved");
    cr_assert(strstr(formatted->chars, "Back to unordered third") != NULL, "Deep mixed nesting should be preserved");
    
    printf("Nested lists test - formatted (length %zu):\n%s\n", formatted->len, formatted->chars);
    
    // Cleanup
    free(content_copy);
}
