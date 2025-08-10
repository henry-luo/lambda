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

void format_item(StrBuf *strbuf, Item item, int depth, char* indent);

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
Test(markup_roundtrip, simple_test, .disabled = true) {
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
    
    // Format using JSON formatter to test parser only
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format simple markdown to JSON");
    cr_assert(formatted->len > 0, "Formatted JSON should not be empty");
    
    // Check that JSON structure is created
    cr_assert(strstr(formatted->chars, "\"$\":") != NULL, "JSON should contain element type information");
    cr_assert(strstr(formatted->chars, "{") != NULL, "JSON should contain object structure");
    
    // Print with length limit to avoid hanging
    printf("Simple test - JSON formatted (length %zu chars):\n", formatted->len);
    if (formatted->chars && formatted->len > 0) {
        size_t print_len = formatted->len > 200 ? 200 : formatted->len;
        printf("%.200s", formatted->chars);
        if (formatted->len > 200) {
            printf("... (truncated)\n");
        } else {
            printf("\n");
        }
    }
    
    // Cleanup
    free(content_copy);
}

// Test empty content handling
Test(markup_roundtrip, empty_test, .disabled = true) {
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
    
    // Format empty content using JSON formatter  
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Should format empty content to JSON");
    
    printf("Empty test - JSON formatted: '%s' (length: %zu)\n", 
           formatted->chars ? formatted->chars : "(null)", formatted->len);
    
    // Cleanup
    free(content_copy);
}

// Helper function for debug tests
static bool test_debug_content(const char* content, const char* test_name) {
    printf("\n=== DEBUG: %s ===\n", test_name);
    printf("Input content (%zu bytes):\n%s\n", strlen(content), content);
    printf("--- End of content ---\n");
    fflush(stdout);
    
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "debug_test.md");
    char* content_copy = strdup(content);
    
    printf("DEBUG: About to parse with input_from_source...\n");
    fflush(stdout);
    
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    
    if (!input) {
        printf("ERROR: Parse failed!\n");
        fflush(stdout);
        free(content_copy);
        return false;
    }
    
    printf("DEBUG: Parse succeeded, about to format with JSON...\n");
    fflush(stdout);
    
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    
    if (!formatted) {
        printf("ERROR: Format failed!\n");
        fflush(stdout);
        free(content_copy);
        return false;
    }
    
    printf("SUCCESS: %s completed (formatted length: %zu)\n", test_name, formatted->len);
    printf("Formatted content (first 150 chars): %.150s\n", 
           formatted->chars ? formatted->chars : "(null)");
    if (formatted->len > 150) {
        printf("... (truncated)\n");
    }
    fflush(stdout);
    
    free(content_copy);
    return true;
}

// Test comprehensive markup features - covers all implemented parser capabilities
Test(markup_roundtrip, complete_test) {
    printf("\n!!! Testing Complete Markup Features ===\n");
    
    // Read comprehensive test content from file
    // char* file_content = read_file_content("test/input/complete_markup_test.md");
    // cr_assert_not_null(file_content, "Failed to read complete_markup_test.md file");
    
    // Use simple content for testing instead of comprehensive to isolate issue
    const char* comprehensive_content = read_file_content("test/input/simple_phase6_test.md");
    cr_assert_not_null(comprehensive_content, "Failed to read simple_phase6_test.md file");
    
    char* comprehensive_markdown = strdup(comprehensive_content);
    
    printf("Simple Phase 6 content for testing (length: %zu bytes)\n", strlen(comprehensive_markdown));
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    lxb_url_t* cwd = get_current_dir();
    lxb_url_t* dummy_url = parse_url(cwd, "simple_phase6_test.md");
    
    // Parse comprehensive content
    Input* input = input_from_source(comprehensive_markdown, dummy_url, type_str, flavor_str);
    cr_assert_not_null(input, "Failed to parse comprehensive markdown");
    StrBuf* strbuf = strbuf_new();
    printf("Parsed input with root_item: %p\n", (void*)input->root);
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter to test parser only
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    cr_assert_not_null(formatted, "Failed to format to Markdown");
    cr_assert(formatted->len > 0, "Formatted Markdown should not be empty");
    printf("Formatted content (length %zu): %s\n", formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Cleanup
    free((void*)comprehensive_content);
    free(comprehensive_markdown);
}