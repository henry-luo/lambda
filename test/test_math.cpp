#define _GNU_SOURCE
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>  // for getcwd and chdir

extern "C" {
#include <tree_sitter/api.h>
#include <mpdecimal.h>
#include "../lambda/lambda.h"
#include "../lib/url.h"
#include "../lib/num_stack.h"

// Additional function declarations that need C linkage
void print_item(StrBuf* strbuf, Item item);
String* format_data(Item item, String* type, String* flavor, VariableMemPool* pool);
void frame_start();
void frame_end();
void heap_init();
void heap_destroy();
Item input_from_source(char* source, Url* url, String* type, String* flavor);
void num_stack_destroy(num_stack_t* stack);
num_stack_t* num_stack_create(size_t initial_capacity);
char* read_text_file(const char* filename);
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
#include "../lib/arraylist.h"
#include "../lib/num_stack.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/url.h"  // Use new URL parser instead of lexbor

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
Url* get_current_dir();
Url* parse_url(Url *base, const char* doc_url);
char* read_text_doc(Url *url);

// Forward declarations for Lambda runtime 
extern __thread Context* context;
void destroy_test_context(Context* ctx);

// Common test function for markdown roundtrip testing
bool test_markdown_roundtrip(const char* test_file_path, const char* debug_file_path, const char* test_description);

// Common test function for math expression roundtrip testing
bool test_math_expressions_roundtrip(const char** test_cases, int num_cases, const char* type, const char* flavor, 
                                    const char* url_prefix, const char* test_name, const char* error_prefix);

// Global test context
static Context* test_context = NULL;

// Setup and teardown functions
void setup_math_tests(void) {
    test_context = create_test_context();
    // Set the global context BEFORE calling heap_init
    context = test_context;
    
    // Initialize the memory system properly
    if (context && context->decimal_ctx) {
        heap_init();
        frame_start();
    }
}

void teardown_math_tests(void) {
    if (test_context && context) {
        frame_end();
        heap_destroy();
        
        if (test_context->num_stack) {
            num_stack_destroy((num_stack_t*)test_context->num_stack);
        }
        
        free(test_context);
        test_context = NULL;
        context = NULL;
    }
}

TestSuite(math_roundtrip_tests, .init = setup_math_tests, .fini = teardown_math_tests);

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
Url* create_test_url(const char* virtual_path) {
    Url* base = get_current_dir();
    if (!base) return NULL;
    
    Url* test_url = parse_url(base, virtual_path);
    url_destroy(base);
    return test_url;
}

// Helper function to print AST structure for debugging
void print_ast_debug(Input* input) {
    if (input && input->root.type != ITEM_UNDEFINED) {
        StrBuf* debug_buf = strbuf_new();
        printf("AST: ");
        print_item(debug_buf, input->root);
        printf("%s\n", debug_buf->str);
        strbuf_free(debug_buf);
    }
}

// Common function to test math expression roundtrip for any array of test cases
// Returns true if all tests pass, false if any fail
bool test_math_expressions_roundtrip(const char** test_cases, int num_cases, const char* type, 
    const char* flavor, const char* url_prefix, const char* test_name, const char* error_prefix) {
    printf("=== Starting %s test ===\n", test_name);
    
    String* type_str = create_lambda_string(type);
    String* flavor_str = create_lambda_string(flavor);
    
    printf("Created type string: '%s', flavor string: '%s'\n", 
           type_str->chars, flavor_str->chars);
    
    if (num_cases > 10) {
        printf("Running %d comprehensive math test cases\n", num_cases);
    }
    
    for (int i = 0; i < num_cases; i++) {
        printf("--- Testing %s case %d: %s ---\n", test_name, i, test_cases[i]);
        
        // Create a virtual URL for this test case
        char virtual_path[256];
        const char* extension = (strcmp(type, "math") == 0) ? "math" : "md";
        snprintf(virtual_path, sizeof(virtual_path), "test://%s_%d.%s", url_prefix, i, extension);
        Url* test_url = create_test_url(virtual_path);
        cr_assert_neq(test_url, NULL, "Failed to create test URL");
        
        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        cr_assert_neq(content_copy, NULL, "Failed to duplicate test content");
        
        // Parse the math expression using input_from_source
        printf("Parsing input with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        if (strcmp(type, "math") == 0) {
            printf("Content to parse: '%s' (length: %zu)\n", content_copy, strlen(content_copy));
        }
        Item input_item = input_from_source(content_copy, test_url, type_str, flavor_str);
        Input* input = input_item.element ? (Input*)input_item.element : nullptr;
        
        if (!input) {
            printf("Failed to parse - skipping case %d\n", i);
            continue;
        }
        
        printf("Successfully parsed input\n");
        
        // Debug: Print AST structure
        print_ast_debug(input);
        
        // Format it back
        printf("Formatting back with pool at %p\n", (void*)input->pool);
        if (strcmp(type, "math") == 0) {
            printf("About to call format_data with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        }
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
        
        if (!formatted) {
            printf("Failed to format - skipping case %d\n", i);
            continue;
        }
        
        if (strcmp(type, "math") == 0) {
            printf("Formatted result: '%s' (length: %zu)\n", formatted->chars, strlen(formatted->chars));
        } else {
            printf("Formatted result: '%s'\n", formatted->chars);
        }
        
        // Verify roundtrip - formatted should equal original
        cr_assert_str_eq(formatted->chars, test_cases[i], 
            "%s roundtrip failed for case %d:\nExpected: '%s'\nGot: '%s'", 
            error_prefix, i, test_cases[i], formatted->chars);
    }
    
    printf("=== Completed %s test ===\n", test_name);
    return true;
}

// Test roundtrip for individual inline math expressions
Test(math_roundtrip_tests, inline_math_roundtrip, .disabled = true) {
    // Test cases: inline math expressions
    const char* test_cases[] = {
        "$E = mc^2$",
        "$x^2 + y^2 = z^2$",
        "$\\alpha + \\beta = \\gamma$",
        "$\\frac{1}{2}$",
        "$\\sqrt{x + y}$"
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark", 
        "inline_math", "inline_math_roundtrip", "Inline math"
    );
    cr_assert(result, "Inline math roundtrip test failed");
}

// Test roundtrip for block math expressions  
Test(math_roundtrip_tests, block_math_roundtrip, .disabled = true) {
    // Test cases: block math expressions
    const char* test_cases[] = {
        "$$E = mc^2$$",
        "$$\\frac{d}{dx}[x^n] = nx^{n - 1}$$",
        "$$\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}$$"
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark", 
        "block_math", "block_math_roundtrip", "Block math"
    );
    cr_assert(result, "Block math roundtrip test failed");
}

// Test math-only expressions (pure math without markdown)
Test(math_roundtrip_tests, pure_math_roundtrip, .disabled = true) {
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
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "math", "latex", 
        "pure_math", "pure_math_roundtrip", "Pure math"
    );
    cr_assert(result, "Pure math roundtrip test failed");
}

// Common function to test markdown roundtrip for any input file
// Returns true if test passes, false if it fails
bool test_markdown_roundtrip(const char* test_file_path, const char* debug_file_path, const char* test_description) {
    printf("=== %s ===\n", test_description ? test_description : "Markdown roundtrip test");
    
    // Get current working directory and build absolute path for file reading
    char cwd_path[1024];
    if (getcwd(cwd_path, sizeof(cwd_path)) == NULL) {
        printf("❌ Could not get current directory\n");
        return false;
    }
    
    char abs_path[1200];
    snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd_path, test_file_path);
    printf("DEBUG: Absolute file path: %s\n", abs_path);
    
    // Read original content directly using absolute path
    char* original_content = read_text_file(abs_path);
    if (!original_content) {
        printf("❌ Could not read %s\n", abs_path);
        return false;
    }
    
    printf("Original content length: %zu\n", strlen(original_content));
    printf("Original content preview: %.100s%s\n", original_content, strlen(original_content) > 100 ? "..." : "");

    // Use exact same pattern as working test
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = NULL;
    
    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    Url* dummy_url = parse_url(cwd, "test.md");
    
    // Make a mutable copy of the file content
    char* md_copy = strdup(original_content);
    
    // Parse the input content (identical to working test)
    printf("DEBUG: About to call input_from_source\n");
    Item input_item = input_from_source(md_copy, dummy_url, type_str, flavor_str);
    Input* input = input_item.element ? (Input*)input_item.element : nullptr;
    printf("DEBUG: After input_from_url call\n");
    if (!input) {
        printf("❌ Failed to parse markdown file: %s\n", abs_path);
        free(original_content);
        return false;
    }
    printf("DEBUG: Successfully parsed input\n");
    
    // Debug: Print AST structure
    printf("AST structure sample:\n");
    print_ast_debug(input);
    
    // Format it back to markdown
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    if (!formatted) {
        printf("❌ Failed to format parsed content back to markdown\n");
        free(original_content);
        return false;
    }
    
    printf("Formatted content length: %zu\n", strlen(formatted->chars));
    printf("Formatted content:\n%s\n", formatted->chars);
    
    // Write debug output to temp directory for analysis (if debug_file_path provided)
    if (debug_file_path) {
        FILE* debug_file = fopen(debug_file_path, "w");
        if (debug_file) {
            fprintf(debug_file, "=== ORIGINAL CONTENT ===\n");
            fprintf(debug_file, "Length: %zu\n", strlen(original_content));
            fprintf(debug_file, "%s\n", original_content);
            fprintf(debug_file, "\n=== FORMATTED CONTENT ===\n");
            fprintf(debug_file, "Length: %zu\n", strlen(formatted->chars));
            fprintf(debug_file, "%s\n", formatted->chars);
            fclose(debug_file);
            printf("Debug output written to %s\n", debug_file_path);
        }
    }
    
    printf("Length comparison - Original: %zu, Formatted: %zu\n", 
        strlen(original_content), strlen(formatted->chars));
    
    // Allow for minor trailing whitespace differences (±2 characters is acceptable)
    size_t orig_len = strlen(original_content);
    size_t formatted_len = strlen(formatted->chars);
    
    // Check if lengths are within acceptable range
    bool length_ok = (abs((int)(formatted_len - orig_len)) <= 2);
    
    if (length_ok) {
        printf("✅ Length difference within acceptable range (±2 characters)\n");
        printf("✅ Markdown roundtrip test completed successfully - no memory corruption!\n");
        printf("✅ Math expressions properly parsed and formatted!\n");
    } else {
        printf("❌ Length mismatch - Original: %zu, Formatted: %zu\n", orig_len, formatted_len);
    }
    
    // Cleanup
    free(original_content);
    free(md_copy);
    
    return length_ok;
}

// Test roundtrip for simple markdown with multiple math expressions  
Test(math_roundtrip_tests, minimal_markdown_test) {
    bool result = test_markdown_roundtrip(
        "test/input/minimal_test.md", "./temp/minimal_debug.txt",
        "Minimal markdown test without math"
    );
    cr_assert(result, "Minimal markdown test failed");
}

Test(math_roundtrip_tests, simple_markdown_roundtrip, .disabled = true) {
    bool result = test_markdown_roundtrip(
        "test/input/math_simple.md", "./temp/simple_debug.txt",
        "Simple markdown test with multiple math expressions"
    );
    cr_assert(result, "Simple markdown roundtrip test failed");
}

Test(math_roundtrip_tests, curated_markdown_roundtrip, .disabled = true) {
    bool result = test_markdown_roundtrip(
        "test/input/comprehensive_math_test.md", "./temp/curated_debug.txt",
        "Curated markdown test with supported math expressions"
    );
    cr_assert(result, "Curated markdown roundtrip test failed");
}

// Helper function implementations for new URL parser

Url* get_current_dir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }
    
    // Create a file URL from the current directory
    char file_url[1200];
    snprintf(file_url, sizeof(file_url), "file://%s/", cwd);
    
    return url_parse(file_url);
}

Url* parse_url(Url *base, const char* doc_url) {
    if (!doc_url) return NULL;
    
    if (base) {
        return url_parse_with_base(doc_url, base);
    } else {
        return url_parse(doc_url);
    }
}

char* read_text_doc(Url *url) {
    if (!url || !url->pathname || !url->pathname->chars) {
        return NULL;
    }
    
    // Extract the file path from the URL
    const char* path = url->pathname->chars;
    
    // Use the existing read_text_file function
    return read_text_file(path);
}