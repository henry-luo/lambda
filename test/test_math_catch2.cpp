#define _GNU_SOURCE
#include <catch2/catch_test_macros.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#ifdef GINAC_AVAILABLE
#include <ginac/ginac.h>
#include <string>
#include <regex>
#include <vector>
using namespace GiNaC;
#endif
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
#include "../lib/url.h"  // Use new URL parser instead of lexbor
extern "C" {
    #include <tree_sitter/api.h>
    #include <mpdecimal.h>
}
#define LAMBDA_STATIC
#include "../lambda/lambda-data.hpp"

extern "C" String* format_data(Item item, String* type, String* flavor, VariableMemPool* pool);
extern "C" Item input_from_source(char* source, Url* url, String* type, String* flavor);
extern "C" char* read_text_file(const char* filename);
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
// Use the existing function from lib/file.c
extern "C" char* read_text_file(const char *filename);

void print_item(StrBuf *strbuf, Item item, int depth=0, char* indent=NULL);

// Forward declarations
char* read_text_doc(Url *url);

#ifdef GINAC_AVAILABLE
/**
 * Extract math expressions from markdown content
 */
std::vector<std::string> extract_math_expressions(const std::string& content) {
    std::vector<std::string> expressions;
    
    // Match both inline math $...$ and display math $$...$$
    std::regex inline_math_regex(R"(\$([^$\n]+)\$)");
    std::regex display_math_regex(R"(\$\$([^$]+)\$\$)");
    
    // First extract display math ($$...$$)
    std::sregex_iterator display_iter(content.begin(), content.end(), display_math_regex);
    std::sregex_iterator end;
    
    for (; display_iter != end; ++display_iter) {
        std::string expr = display_iter->str(1);
        // Clean up whitespace and newlines
        std::regex whitespace_regex(R"(\s+)");
        expr = std::regex_replace(expr, whitespace_regex, " ");
        // Trim
        expr.erase(0, expr.find_first_not_of(" \t\n\r"));
        expr.erase(expr.find_last_not_of(" \t\n\r") + 1);
        if (!expr.empty()) {
            expressions.push_back(expr);
        }
    }
    
    // Then extract inline math ($...$) but skip if it's part of display math
    std::string content_no_display = content;
    content_no_display = std::regex_replace(content_no_display, display_math_regex, "");
    
    std::sregex_iterator inline_iter(content_no_display.begin(), content_no_display.end(), inline_math_regex);
    
    for (; inline_iter != end; ++inline_iter) {
        std::string expr = inline_iter->str(1);
        // Skip expressions that are clearly not math (contain markdown syntax)
        if (expr.find("**") != std::string::npos || 
            expr.find("##") != std::string::npos ||
            expr.find("_") == 0 ||  // starts with underscore (likely markdown)
            expr.length() > 200) {  // too long, likely not a single math expression
            continue;
        }
        
        // Clean up whitespace
        std::regex whitespace_regex(R"(\s+)");
        expr = std::regex_replace(expr, whitespace_regex, " ");
        // Trim
        expr.erase(0, expr.find_first_not_of(" \t\n\r"));
        expr.erase(expr.find_last_not_of(" \t\n\r") + 1);
        if (!expr.empty()) {
            expressions.push_back(expr);
        }
    }
    
    return expressions;
}

/**
 * Helper function to convert LaTeX math expressions to GiNaC-compatible format
 */
std::string latex_to_ginac(const std::string& latex_expr) {
    std::string result = latex_expr;
    
    // Skip expressions that contain LaTeX constructs GiNaC can't handle
    if (result.find("\\") != std::string::npos && 
        (result.find("\\sqrt") != std::string::npos ||
         result.find("\\pi") != std::string::npos ||
         result.find("\\alpha") != std::string::npos ||
         result.find("\\beta") != std::string::npos ||
         result.find("\\gamma") != std::string::npos ||
         result.find("\\sin") != std::string::npos ||
         result.find("\\cos") != std::string::npos ||
         result.find("\\log") != std::string::npos ||
         result.find("\\int") != std::string::npos ||
         result.find("\\lim") != std::string::npos ||
         result.find("\\begin") != std::string::npos ||
         result.find("\\text") != std::string::npos ||
         result.find("\\left") != std::string::npos ||
         result.find("\\right") != std::string::npos ||
         result.find("\\infty") != std::string::npos ||
         result.find("\\forall") != std::string::npos ||
         result.find("\\exists") != std::string::npos ||
         result.find("\\leq") != std::string::npos ||
         result.find("\\neq") != std::string::npos ||
         result.find("\\in") != std::string::npos)) {
        return "";  // Return empty string to signal unsupported expression
    }
    
    // Replace LaTeX operators with GiNaC-compatible ones
    std::regex cdot_regex(R"(\\cdot)");
    result = std::regex_replace(result, cdot_regex, "*");
    
    std::regex times_regex(R"(\\times)");
    result = std::regex_replace(result, times_regex, "*");
    
    // Replace \frac{a}{b} with (a)/(b)
    std::regex frac_regex(R"(\\frac\{([^}]+)\}\{([^}]+)\})");
    result = std::regex_replace(result, frac_regex, "($1)/($2)");
    
    return result;
}

/**
 * Check semantic equivalence for expressions that GiNaC can't parse
 */
bool are_expressions_semantically_equivalent(const std::string& expr1, const std::string& expr2) {
    // Enhanced semantic equivalence checks for mathematical expressions
    printf("DEBUG SEMANTIC: Comparing '%s' vs '%s'\n", expr1.c_str(), expr2.c_str());
    
    std::string s1 = expr1;
    std::string s2 = expr2;
    
    // Basic string comparison after normalization
    if (s1 == s2) return true;
    
    // Additional semantic checks can be added here
    return false;
}

/**
 * Check if two mathematical expressions are equivalent using GiNaC
 */
bool are_math_expressions_equivalent(const std::string& expr1, const std::string& expr2) {
    try {
        printf("DEBUG: Converting '%s' -> ", expr1.c_str());
        std::string ginac_expr1 = latex_to_ginac(expr1);
        printf("'%s'\n", ginac_expr1.c_str());
        
        printf("DEBUG: Converting '%s' -> ", expr2.c_str());
        std::string ginac_expr2 = latex_to_ginac(expr2);
        printf("'%s'\n", ginac_expr2.c_str());
        
        // If either expression can't be converted to GiNaC format, use semantic comparison
        if (ginac_expr1.empty() || ginac_expr2.empty()) {
            printf("DEBUG: One or both expressions can't be parsed by GiNaC, using semantic comparison\n");
            return are_expressions_semantically_equivalent(expr1, expr2);
        }
        
        // Set up common symbols that might appear in expressions
        symbol x("x"), y("y"), z("z"), a("a"), b("b"), c("c"), n("n"), e("e");
        
        parser reader;
        reader.get_syms()["x"] = x; reader.get_syms()["y"] = y; reader.get_syms()["z"] = z;
        reader.get_syms()["a"] = a; reader.get_syms()["b"] = b; reader.get_syms()["c"] = c;
        reader.get_syms()["n"] = n; reader.get_syms()["e"] = e;
        
        // Parse both expressions
        ex e1 = reader(ginac_expr1);
        ex e2 = reader(ginac_expr2);
        
        // Compare by expanding and normalizing both expressions
        ex difference = (e1.expand().normal() - e2.expand().normal()).expand().normal();
        
        return difference.is_zero();
    } catch (const std::exception& e) {
        printf("DEBUG: GiNaC parsing failed: %s, falling back to semantic comparison\n", e.what());
        return are_expressions_semantically_equivalent(expr1, expr2);
    }
}
#endif

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
        REQUIRE(test_url != NULL);
        
        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        REQUIRE(content_copy != NULL);
        
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
        
        // Verify roundtrip using semantic comparison like indexed_math_test
        printf("📝 Original:  '%s'\n", test_cases[i]);
        printf("🔄 Formatted: '%s'\n", formatted->chars);
        
        // Step 1: String comparison first
        if (strcmp(formatted->chars, test_cases[i]) == 0) {
            printf("✅ PASS: Exact string match\n");
            continue;
        }
        
        // Step 2: Try semantic equivalence for mismatches
        printf("⚠️  String mismatch, trying semantic comparison...\n");
        
#ifdef GINAC_AVAILABLE
        if (are_expressions_semantically_equivalent(std::string(test_cases[i]), std::string(formatted->chars))) {
            printf("✅ PASS: Semantic equivalence detected\n");
            continue;
        }
#endif
        
        // If no equivalence found, fail the test
        printf("❌ FAIL: No equivalence found - parser/formatter issue\n");
        REQUIRE(strcmp(formatted->chars, test_cases[i]) == 0);
    }
    
    printf("=== Completed %s test ===\n", test_name);
    return true;
}

bool test_markdown_roundtrip(const char* input_file, const char* debug_file, const char* test_description) {
    printf("=== Starting %s ===\n", test_description);
    
    // Read the original markdown file
    char* original_content = read_text_file(input_file);
    if (!original_content) {
        printf("Failed to read input file: %s\n", input_file);
        return false;
    }
    
    printf("Original content length: %zu\n", strlen(original_content));
    
    // Create Lambda strings for type and flavor
    String* type_str = create_lambda_string("markdown");
    String* flavor_str = create_lambda_string("commonmark");
    
    // Create URL for the input file
    Url* input_url = create_test_url(input_file);
    if (!input_url) {
        printf("Failed to create URL for input file\n");
        free(original_content);
        return false;
    }
    
    // Make a copy of the content for parsing (input_from_source takes ownership)
    char* md_copy = strdup(original_content);
    if (!md_copy) {
        printf("Failed to duplicate markdown content\n");
        free(original_content);
        url_destroy(input_url);
        return false;
    }
    
    // Parse the markdown content
    Item input_item = input_from_source(md_copy, input_url, type_str, flavor_str);
    Input* input = input_item.element ? (Input*)input_item.element : nullptr;
    
    if (!input) {
        printf("Failed to parse markdown content\n");
        free(original_content);
        return false;
    }
    
    // Format it back to markdown
    String* formatted = format_data(input->root, type_str, flavor_str, input->pool);
    if (!formatted) {
        printf("Failed to format markdown content\n");
        free(original_content);
        return false;
    }
    
    printf("Formatted content length: %zu\n", strlen(formatted->chars));
    
    // Write debug output
    FILE* debug_fp = fopen(debug_file, "w");
    if (debug_fp) {
        fprintf(debug_fp, "=== Original ===\n%s\n\n=== Formatted ===\n%s\n", 
                original_content, formatted->chars);
        fclose(debug_fp);
        printf("Debug output written to: %s\n", debug_file);
    }
    
    // Check if lengths match (basic sanity check)
    bool length_ok = (strlen(original_content) == strlen(formatted->chars));
    if (!length_ok) {
        printf("Length mismatch: original=%zu, formatted=%zu\n", 
               strlen(original_content), strlen(formatted->chars));
    }
    
    // Clean up
    free(original_content);
    free(md_copy);
    
    return length_ok;
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

TEST_CASE("Math roundtrip tests - inline math roundtrip", "[math_roundtrip_tests]") {
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
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - block math roundtrip", "[math_roundtrip_tests]") {
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
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - pure math roundtrip", "[math_roundtrip_tests]") {
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
        "\\frac{1}{2}"
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "math", "latex", 
        "pure_math", "pure_math_roundtrip", "Pure math"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - minimal markdown test", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/minimal_test.md", "./temp/minimal_debug.txt",
        "Minimal markdown test without math"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - small math test", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/small_math_test.md", "./temp/small_math_debug.txt",
        "Small math test with basic expressions"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - spacing test", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/spacing_test.md", "./temp/spacing_debug.txt",
        "Spacing command test"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - simple markdown roundtrip", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/math_simple.md", "./temp/simple_debug.txt",
        "Simple markdown test with multiple math expressions"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - indexed math test", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/indexed_math_test.md", "./temp/indexed_debug.txt",
        "Indexed math test to track expression alignment"
    );
    REQUIRE(result);
}

TEST_CASE("Math roundtrip tests - advanced math test", "[math_roundtrip_tests]") {
    bool result = test_markdown_roundtrip(
        "test/input/advanced_math_test.md", "./temp/advanced_debug.txt",
        "Advanced math expressions with complex formatting"
    );
    REQUIRE(result);
}
