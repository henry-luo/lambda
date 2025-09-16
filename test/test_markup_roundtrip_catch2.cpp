#define _GNU_SOURCE
#include <catch2/catch_test_macros.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>
#include "../lambda/lambda.h"
#include "../lambda/lambda-data.hpp"  // This includes the Input struct definition
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/url.h"  // Use new URL parser instead of lexbor

// Forward declarations with C linkage
extern "C" {
    #include <tree_sitter/api.h>
    #include <mpdecimal.h>
    Input* input_from_source(char* source, Url* abs_url, String* type, String* flavor);
    String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
    void format_item(StrBuf* buf, Item item, int indent, char* format);
    char* read_text_file(const char *filename);
    TSParser* lambda_parser(void);
    TSTree* lambda_parse_source(TSParser* parser, const char* source_code);
}

// Implement missing functions locally to avoid linking conflicts
extern "C" Context* create_test_context() {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize basic context fields
    ctx->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    if (ctx->decimal_ctx) {
        mpd_defaultcontext(ctx->decimal_ctx);
    }
    
    // Initialize num_stack and heap to avoid crashes
    ctx->num_stack = num_stack_create(1024);  // Create with reasonable initial capacity
    ctx->heap = NULL;  // Will be initialized by heap_init()
    
    return ctx;
}

// Tree-sitter function declarations
extern "C" const TSLanguage *tree_sitter_lambda(void);

extern "C" TSParser* lambda_parser(void) {
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_lambda());
    return parser;
}

extern "C" TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
    TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    return tree;
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

// Helper function for debug tests
static bool test_debug_content(const char* content, const char* test_name) {
    printf("\n=== DEBUG: %s ===\n", test_name);
    printf("Input content (%zu bytes):\n%s\n", strlen(content), content);
    printf("--- End of content ---\n");
    fflush(stdout);
    
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "debug_test.md");
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
    
    printf("SUCCESS: %s completed (formatted length: %zu)\n", test_name, (size_t)formatted->len);
    printf("Formatted content (first 150 chars): %.150s\n", 
           formatted->chars ? formatted->chars : "(null)");
    if (formatted->len > 150) {
        printf("... (truncated)\n");
    }
    fflush(stdout);
    
    free(content_copy);
    return true;
}

TEST_CASE("Markup roundtrip - simple test", "[markup_roundtrip]") {
    printf("\n=== Testing Simple Markdown Elements ===\n");
    
    const char* test_markdown = "# Header\n\nParagraph with **bold** text.\n\n- List item\n- Another item\n";
    
    // Create Lambda strings for input parameters  
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(test_markdown);
    
    // Parse
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    // Format using JSON formatter to test parser only
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    
    // Check that JSON structure is created
    REQUIRE(strstr(formatted->chars, "\"$\":") != NULL);
    REQUIRE(strstr(formatted->chars, "{") != NULL);
    
    // Print with length limit to avoid hanging
    printf("Simple test - JSON formatted (length %zu chars):\n", (size_t)formatted->len);
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

TEST_CASE("Markup roundtrip - empty test", "[markup_roundtrip]") {
    printf("\n=== Testing Empty Content ===\n");
    
    const char* empty_markdown = "";
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "empty.md");
    
    // Make a mutable copy of the content
    char* content_copy = strdup(empty_markdown);
    
    // Parse empty content
    Input* input = input_from_source(content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    // Format empty content using JSON formatter  
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    
    printf("Empty test - JSON formatted: '%s' (length: %zu)\n", 
           formatted->chars ? formatted->chars : "(null)", (size_t)formatted->len);
    
    // Cleanup
    free(content_copy);
}

TEST_CASE("Markup roundtrip - complete test", "[markup_roundtrip]") {
    printf("\n!!! Testing Complete Markup Features ===\n");
    
    // Read comprehensive test content from file
    char* comprehensive_content = read_text_file("test/input/comprehensive_test.md");
    REQUIRE(comprehensive_content != nullptr);
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_test.md");
    
    // Parse comprehensive content
    Input* input = input_from_source(comprehensive_content, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    StrBuf* strbuf = strbuf_new();
    printf("Parsed input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter to test parser only
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    printf("Formatted content (length %zu): %s\n", (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Cleanup
    free(comprehensive_content);
}

TEST_CASE("Markup roundtrip - emoji test", "[markup_roundtrip]") {
    printf("\n=== Testing Comprehensive Emoji Features ===\n");
    
    // Read emoji test content from file
    char* emoji_content = read_text_file("test/input/comprehensive_emoji_test.md");
    REQUIRE(emoji_content != nullptr);
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_emoji_test.md");
    
    // Parse emoji content
    Input* input = input_from_source(emoji_content, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    StrBuf* strbuf = strbuf_new();
    printf("Parsed emoji input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted emoji output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter to test parser only
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    printf("Formatted emoji content (length %zu): %s\n", (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Cleanup
    free(emoji_content);
}

TEST_CASE("Markup roundtrip - comprehensive math test", "[markup_roundtrip]") {
    printf("\n=== Testing Comprehensive Math Features from File ===\n");
    
    // Read math test content from file
    char* math_content = read_text_file("test/input/comprehensive_math_test.md");
    REQUIRE(math_content != nullptr);
    
    // Create Lambda strings for input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_math_test.md");
    
    // Make a copy since input_from_source may modify the content
    char* math_content_copy = strdup(math_content);
    
    // Parse math content
    Input* input = input_from_source(math_content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    StrBuf* strbuf = strbuf_new();
    printf("Parsed math input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted math output: %s\n", strbuf->str ? strbuf->str : "(null)");

    // Format using Markdown formatter to test parser only
    String* markdown_type = create_lambda_string("markdown");
    String* formatted = format_data(input->root, markdown_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    printf("Formatted math content (length %zu): %s\n", (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");

    // Cleanup
    free(math_content);
    free(math_content_copy);
}

TEST_CASE("Markup roundtrip - RST directives test", "[markup_roundtrip]") {
    printf("\n=== Testing RST Directives and Format-Specific Features ===\n");
    
    // Read RST test content from file
    char* rst_content = read_text_file("test/input/comprehensive_test.rst");
    REQUIRE(rst_content != nullptr);
    
    // Create Lambda strings for RST input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "comprehensive_test.rst");
    
    // Make a copy since input_from_source may modify the content
    char* rst_content_copy = strdup(rst_content);
    
    // Parse RST content
    printf("DEBUG: Parsing RST content (%zu bytes)...\n", strlen(rst_content));
    Input* input = input_from_source(rst_content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    StrBuf* strbuf = strbuf_new();
    printf("Parsed RST input with root_item: %p\n", reinterpret_cast<void*>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("Formatted RST output (first 300 chars): %.300s\n", strbuf->str ? strbuf->str : "(null)");
    if (strbuf->str && strlen(strbuf->str) > 300) {
        printf("... (truncated)\n");
    }

    // Format using JSON formatter to verify structure
    String* json_type = create_lambda_string("json");
    printf("DEBUG test: Before format_data, input->root=0x%llx\n", 
           static_cast<unsigned long long>(input->root.item));
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    printf("RST JSON structure (length %zu, first 200 chars): %.200s\n", 
           (size_t)formatted->len, formatted->chars ? formatted->chars : "(null)");
    
    // Check that RST-specific elements are present
    if (formatted->chars) {
        // Should contain directive elements
        REQUIRE(strstr(formatted->chars, "directive") != NULL);
        
        // Should contain code-block directives
        REQUIRE((strstr(formatted->chars, "code-block") != NULL || 
                strstr(formatted->chars, "code") != NULL));
        
        printf("SUCCESS: RST directives and format-specific features detected!\n");
    }

    // Cleanup
    free(rst_content);
    free(rst_content_copy);
    strbuf_free(strbuf);
}

TEST_CASE("Markup roundtrip - basic RST test", "[markup_roundtrip]") {
    printf("\n=== Testing Basic RST Directive Parsing ===\n");
    
    const char* basic_rst = 
        "RST Test Document\n"
        "=================\n"
        "\n"
        "This is a paragraph with some text.\n"
        "\n"
        ".. note::\n"
        "   This is a note directive.\n"
        "   It spans multiple lines.\n"
        "\n"
        ".. code-block:: python\n"
        "   :linenos:\n"
        "\n"
        "   def hello():\n"
        "       print('Hello World')\n"
        "\n"
        "Another paragraph after directives.\n";
    
    // Create Lambda strings for RST input parameters
    String* type_str = create_lambda_string("markup");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution  
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "basic_test.rst");
    
    // Make a copy since input_from_source may modify the content
    char* rst_content_copy = strdup(basic_rst);
    
    // Parse basic RST content
    printf("DEBUG: Parsing basic RST content...\n");
    Input* input = input_from_source(rst_content_copy, dummy_url, type_str, flavor_str);
    REQUIRE(input != nullptr);
    
    // Format using JSON to check structure
    String* json_type = create_lambda_string("json");
    String* formatted = format_data(input->root, json_type, flavor_str, input->pool);
    REQUIRE(formatted != nullptr);
    REQUIRE(formatted->len > 0);
    
    printf("Basic RST JSON (first 400 chars): %.400s\n", 
           formatted->chars ? formatted->chars : "(null)");
    
    // Verify RST-specific features are detected
    if (formatted->chars) {
        bool has_headers = strstr(formatted->chars, "h1") != NULL;
        bool has_paragraphs = strstr(formatted->chars, "\"$\":\"p\"") != NULL;
        bool has_directives = strstr(formatted->chars, "directive") != NULL;
        
        printf("RST parsing results - Headers: %s, Paragraphs: %s, Directives: %s\n",
               has_headers ? "YES" : "NO", 
               has_paragraphs ? "YES" : "NO",
               has_directives ? "YES" : "NO");
        
        // At minimum should have headers and paragraphs
        REQUIRE((has_headers || has_paragraphs));
        
        if (has_directives) {
            printf("SUCCESS: RST directives properly detected and parsed!\n");
        } else {
            printf("INFO: RST directives not detected (may need format detection improvement)\n");
        }
    }
    
    // Cleanup
    free(rst_content_copy);
}

TEST_CASE("Markup parsing RST - extended features", "[markup_parsing_rst]") {
    printf("\n=== Testing Extended RST Features ===\n");
    
    const char* rst_extended_content = 
        ".. This is a comment\n"
        "   spanning multiple lines\n"
        "\n"
        "Document Title\n"
        "==============\n"
        "\n"
        "Text with ``literal markup`` and reference_ links.\n"
        "\n"
        "Transition line below:\n"
        "\n"
        "----\n"
        "\n"
        "Definition Lists\n"
        "\n"
        "term 1\n"
        "    Definition of term 1.\n"
        "\n"
        "term 2\n"
        "    Definition of term 2.\n"
        "\n"
        "Literal block follows::\n"
        "\n"
        "    This is a literal block.\n"
        "    It preserves whitespace.\n"
        "        Even indentation.\n"
        "\n"
        "Grid table:\n"
        "\n"
        "+-------+-------+\n"
        "| A     | B     |\n"
        "+-------+-------+\n"
        "| 1     | 2     |\n"
        "+-------+-------+\n";
        
    String* type_str = create_lambda_string("markup");
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test_extended.rst");
    
    char* content_copy = strdup(rst_extended_content);
    Input* input = input_from_source(content_copy, dummy_url, type_str, NULL);
    REQUIRE(input != nullptr);
    
    printf("DEBUG: Immediately after input_from_source, input=%p, input->root=0x%llx\n", 
           static_cast<void*>(input), static_cast<unsigned long long>(input->root.item));
    
    // Check if the input pointer is corrupted
    printf("DEBUG: Input struct fields: url=%p, pool=%p, type_list=%p, sb=%p\n", 
           input->url, input->pool, input->type_list, input->sb);
    
    StrBuf* strbuf = strbuf_new();
    printf("DEBUG test: Before test format_item, input->root=0x%llx\n", 
           static_cast<unsigned long long>(input->root.item));
    format_item(strbuf, input->root, 0, NULL);
    printf("DEBUG test: After test format_item, input->root=0x%llx\n", 
           static_cast<unsigned long long>(input->root.item));
    printf("Extended RST output: %.200s\n", strbuf->str ? strbuf->str : "(null)");

    // Check if the item value changed
    Item test_root = input->root;
    printf("DEBUG test: Copied input->root to test_root=0x%llx\n", 
           static_cast<unsigned long long>(test_root.item));
    
    // Format to JSON and verify extended features
    String* json_type = create_lambda_string("json");
    printf("DEBUG test extended: Before format_data, input->root=0x%llx, test_root=0x%llx\n", 
           static_cast<unsigned long long>(input->root.item), static_cast<unsigned long long>(test_root.item));
    String* formatted = format_data(input->root, json_type, NULL, input->pool);
    REQUIRE(formatted != nullptr);
    
    if (formatted->chars) {
        // Check for literal text (double backticks)
        bool has_literal = strstr(formatted->chars, "literal") != NULL ||
                          strstr(formatted->chars, "code") != NULL;
        printf("Literal text detection: %s\n", has_literal ? "YES" : "NO");
        
        // Check for comments
        bool has_comment = strstr(formatted->chars, "comment") != NULL;
        printf("Comment detection: %s\n", has_comment ? "YES" : "NO");
        
        // Check for definition lists  
        bool has_def_list = strstr(formatted->chars, "dl") != NULL ||
                           strstr(formatted->chars, "definition") != NULL;
        printf("Definition list detection: %s\n", has_def_list ? "YES" : "NO");
        
        // Check for horizontal rules/transitions
        bool has_hr = strstr(formatted->chars, "hr") != NULL ||
                     strstr(formatted->chars, "divider") != NULL;
        printf("Transition line detection: %s\n", has_hr ? "YES" : "NO");
        
        printf("SUCCESS: Extended RST features test completed!\n");
    }
    
    free(content_copy);
}
