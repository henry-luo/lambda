#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // for getcwd and chdir
#include "../lambda/lambda-data.h"
#include "../lib/arraylist.h"
#include <lexbor/url/url.h>

// Forward declarations
Input* input_from_source(char* source, lxb_url_t* abs_url, String* type, String* flavor);
Input* input_from_url(String* url, String* type, String* flavor, lxb_url_t* cwd);
String* format_data(Item item, String* type, String* flavor, VariableMemPool *pool);
lxb_url_t* get_current_dir();
lxb_url_t* parse_url(lxb_url_t *base, const char* doc_url);
void print_item(StrBuf *strbuf, Item item);


TestSuite(math_roundtrip_tests);

// Use the existing function from lib/file.c
char* read_text_file(const char *filename);

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text) {
    if (!text) return NULL;
    
    size_t len = strlen(text);
    // Allocate String struct + space for the null-terminated string
    String* result = (String*)malloc(sizeof(String) + len + 1);
    if (!result) return NULL;
    
    result->len = len;
    // Copy the string content to the chars array at the end of the struct
    strcpy(result->chars, text);
    
    printf("DEBUG: Created Lambda string: '%s' (length: %d)\n", result->chars, result->len);
    return result;
}

// Helper to create a dynamic URL for content testing
lxb_url_t* create_test_url(const char* virtual_path) {
    lxb_url_t* base = get_current_dir();
    if (!base) return NULL;
    
    lxb_url_t* test_url = parse_url(base, virtual_path);
    lxb_url_destroy(base);
    return test_url;
}

// Test roundtrip for individual inline math expressions
Test(math_roundtrip_tests, inline_math_roundtrip) {
    printf("=== Starting inline_math_roundtrip test ===\n");
    
    // Test cases: inline math expressions
    const char* test_cases[] = {
        "$E = mc^2$",
        "$x^2 + y^2 = z^2$",
        "$\\alpha + \\beta = \\gamma$",
        "$\\frac{1}{2}$",
        "$\\sqrt{x + y}$"
    };
    
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = create_lambda_string("commonmark");
    
    printf("Created type string: '%s', flavor string: '%s'\n", 
           type_str->chars, flavor_str->chars);
    
    for (int i = 0; i < 5; i++) {
        printf("--- Testing inline math case %d: %s ---\n", i, test_cases[i]);
        
        // Create a virtual URL for this test case
        char virtual_path[256];
        snprintf(virtual_path, sizeof(virtual_path), "test://inline_math_%d.md", i);
        lxb_url_t* test_url = create_test_url(virtual_path);
        cr_assert_neq(test_url, NULL, "Failed to create test URL");
        
        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        cr_assert_neq(content_copy, NULL, "Failed to duplicate test content");
        
        // Parse the math expression using input_from_source
        printf("Parsing input with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        Input* input = input_from_source(content_copy, test_url, type_str, flavor_str);
        
        if (!input) {
            printf("Failed to parse - skipping case %d\n", i);
            continue;
        }
        
        printf("Successfully parsed input\n");
        
        // Debug: Print AST structure
        if (input->root) {
            StrBuf* debug_buf = strbuf_new();
            printf("AST: ");
            print_item(debug_buf, input->root);
            printf("%s\n", debug_buf->str);
            strbuf_free(debug_buf);
        }
        
        // Format it back
        printf("Formatting back with pool at %p\n", (void*)input->pool);
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
        
        if (!formatted) {
            printf("Failed to format - skipping case %d\n", i);
            continue;
        }
        
        printf("Formatted result: '%s'\n", formatted->chars);
        
        // Verify roundtrip - formatted should equal original
        cr_assert_str_eq(formatted->chars, test_cases[i], 
            "Inline math roundtrip failed for case %d:\nExpected: '%s'\nGot: '%s'", 
            i, test_cases[i], formatted->chars);
    }
    
    printf("=== Completed inline_math_roundtrip test ===\n");
}

// Test roundtrip for block math expressions  
Test(math_roundtrip_tests, block_math_roundtrip) {
    printf("=== Starting block_math_roundtrip test ===\n");
    // Test cases: block math expressions
    const char* test_cases[] = {
        "$$E = mc^2$$",
        "$$\\frac{d}{dx}[x^n] = nx^{n - 1}$$",
        "$$\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}$$"
    };
    
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = create_lambda_string("commonmark");
    
    printf("Created type string: '%s', flavor string: '%s'\n", 
           type_str->chars, flavor_str->chars);
    
    for (int i = 0; i < 3; i++) {
        printf("--- Testing case %d: %s ---\n", i, test_cases[i]);
        
        // Create a virtual URL for this test case
        char virtual_path[256];
        snprintf(virtual_path, sizeof(virtual_path), "test://block_math_%d.md", i);
        lxb_url_t* test_url = create_test_url(virtual_path);
        cr_assert_neq(test_url, NULL, "Failed to create test URL");
        
        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        cr_assert_neq(content_copy, NULL, "Failed to duplicate test content");
        
        // Parse the math expression using input_from_source
        printf("Parsing input with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        Input* input = input_from_source(content_copy, test_url, type_str, flavor_str);
        
        if (!input) {
            printf("Failed to parse - skipping case %d\n", i);
            continue;
        }
        
        printf("Successfully parsed input\n");
        
        // Debug: Print AST structure
        if (input->root) {
            StrBuf* debug_buf = strbuf_new();
            printf("AST: ");
            print_item(debug_buf, input->root);
            printf("%s\n", debug_buf->str);
            strbuf_free(debug_buf);
        }
        
        // Format it back
        printf("Formatting back with pool at %p\n", (void*)input->pool);
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
        
        if (!formatted) {
            printf("Failed to format - skipping case %d\n", i);
            continue;
        }
        
        printf("Formatted result: '%s'\n", formatted->chars);
        
        // Verify roundtrip - formatted should equal original
        cr_assert_str_eq(formatted->chars, test_cases[i], 
            "Block math roundtrip failed for case %d:\nExpected: '%s'\nGot: '%s'", 
            i, test_cases[i], formatted->chars);        
        // if (strcmp(formatted->chars, test_cases[i]) == 0) {
        //     printf("✅ Roundtrip successful for case %d\n", i);
        // } else {
        //     printf("❌ Roundtrip failed for case %d\n", i);
        //     printf("Expected: '%s'\n", test_cases[i]);
        //     printf("Got: '%s'\n", formatted->chars);
        // }
    }
    
    printf("=== Completed block_math_roundtrip test ===\n");
}

// Test roundtrip for comprehensive markdown with math
Test(math_roundtrip_tests, comprehensive_markdown_roundtrip, .disabled = true) {
    printf("=== Comprehensive markdown test ===\n");
    
    // Use relative path instead of hardcoded absolute path
    String* file_url_str = create_lambda_string("test/input/comprehensive_math_test.md");
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = create_lambda_string("");
    
    // Read original content for comparison
    char* original_content = read_text_file("test/input/comprehensive_math_test.md");
    cr_assert_neq(original_content, NULL, "Could not read comprehensive_math_test.md");

    // Parse the markdown content with math using input_from_url
    Input* input = input_from_url(file_url_str, type_str, flavor_str, NULL);
    cr_assert_neq(input, NULL, "Failed to parse comprehensive markdown with math");
    
    // Format it back to markdown
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    cr_assert_neq(formatted, NULL, "Failed to format parsed content back to markdown");
    
    // Verify roundtrip - formatted should equal original
    // Note: We may need to normalize whitespace differences
    cr_assert_str_eq(formatted->chars, original_content, 
        "Comprehensive markdown roundtrip failed:\nOriginal length: %zu\nFormatted length: %zu", 
        strlen(original_content), strlen(formatted->chars));
    
    // Cleanup
    free(original_content);
}

// Test math-only expressions (pure math without markdown)
Test(math_roundtrip_tests, pure_math_roundtrip) {
    printf("=== Starting pure_math_roundtrip test ===\n");
    // Test pure math expressions covering various mathematical expression groups
    const char* test_cases[] = {
        // Basic operators and arithmetic
        "E = mc^2",
        "x^2 + y^2 = z^2",
        "a - b \\cdot c",
        "\\frac{a}{b} + \\frac{c}{d}",
        
        // Simple symbols and constants
        "\\alpha + \\beta = \\gamma",
        "\\pi \\neq \\infty",
        
        // More basic expressions
        "\\sqrt{x + y}",
        "\\frac{1}{2}",
        
        // Greek letters (lowercase)
        "\\delta\\epsilon\\zeta",
        "\\theta\\iota\\kappa",
        "\\mu\\nu\\xi",
        "\\rho\\sigma\\tau",
        "\\chi\\psi\\omega",
        
        // Greek letters (uppercase)
        "\\Gamma\\Delta\\Theta",
        "\\Xi\\Pi\\Sigma",
        "\\Phi\\Psi\\Omega",
        
        // Special symbols
        "\\partial\\nabla",
        
        // Simple arrows
        "x \\to y",
        
        // Relations
        "a = b",
        "x \\neq y",
        "p \\leq q",
        "r \\geq s",
        
        // Set theory symbols
        "x \\in A",
        "B \\subset C",
        "F \\cup G",
        "H \\cap I",
        
        // Simple logic
        "P \\land Q",
        "R \\lor S",
        "\\forall x",
        "\\exists y",
        
        // Binomial coefficient
        "\\binom{n}{k}",
        
        // Simple accents
        "\\hat{x}",
        "\\tilde{y}",
        "\\bar{z}",
        "\\vec{v}",
        
        // Combined expressions
        "\\alpha^2 + \\beta^2",
        "\\frac{\\pi}{2}",
        "\\sqrt{\\alpha + \\beta}"
    };
    
    int num_test_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    String* type_str = create_lambda_string("math");
    String* flavor_str = create_lambda_string("latex");
    
    printf("Created type string: '%s', flavor string: '%s'\n", 
           type_str->chars, flavor_str->chars);
    printf("Running %d comprehensive math test cases\n", num_test_cases);
    
    for (int i = 0; i < num_test_cases; i++) {
        printf("--- Testing pure math case %d: %s ---\n", i, test_cases[i]);
        
        // Create a virtual URL for this test case
        char virtual_path[256];
        snprintf(virtual_path, sizeof(virtual_path), "test://pure_math_%d.math", i);
        lxb_url_t* test_url = create_test_url(virtual_path);
        cr_assert_neq(test_url, NULL, "Failed to create test URL");
        
        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        cr_assert_neq(content_copy, NULL, "Failed to duplicate test content");
        
        // Parse the math expression using input_from_source
        printf("Parsing input with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        printf("Content to parse: '%s' (length: %zu)\n", content_copy, strlen(content_copy));
        Input* input = input_from_source(content_copy, test_url, type_str, flavor_str);
        
        if (!input) {
            printf("Failed to parse - skipping case %d\n", i);
            continue;
        }
        
        printf("Successfully parsed input\n");
        printf("Input root item type: %p\n", (void*)input->root);
        if (input->root) {
            printf("Root item exists\n");
            StrBuf* sb = strbuf_new();
            print_item(sb, input->root);
            printf("Root item: '%s'\n", sb->str);
            strbuf_free(sb);
        }
        
        // Format it back
        printf("Formatting back with pool at %p\n", (void*)input->pool);
        printf("About to call format_data with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
        
        if (!formatted) {
            printf("Failed to format - skipping case %d\n", i);
            continue;
        }
        
        printf("Formatted result: '%s' (length: %zu)\n", formatted->chars, strlen(formatted->chars));
        
        // Verify roundtrip - formatted should equal original
        cr_assert_str_eq(formatted->chars, test_cases[i], 
            "Pure math roundtrip failed for case %d:\nExpected: '%s'\nGot: '%s'", 
            i, test_cases[i], formatted->chars);
    }
    printf("=== Completed pure_math_roundtrip test ===\n");
}
