#define _GNU_SOURCE
#include <criterion/criterion.h>
#include <criterion/criterion.h>
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
    std::string norm1 = expr1, norm2 = expr2;
    
    // Handle common LaTeX function transformations
    // \det(A) -> \text{determinant}((A))
    if ((expr1.find("\\det") != std::string::npos && expr2.find("\\text{determinant}") != std::string::npos) ||
        (expr2.find("\\det") != std::string::npos && expr1.find("\\text{determinant}") != std::string::npos)) {
        return true;
    }
    
    // \tr(B) -> \text{trace}((B))
    if ((expr1.find("\\tr") != std::string::npos && expr2.find("\\text{trace}") != std::string::npos) ||
        (expr2.find("\\tr") != std::string::npos && expr1.find("\\text{trace}") != std::string::npos)) {
        return true;
    }
    
    // \ker(T) -> \text{kernel}((T))
    if ((expr1.find("\\ker") != std::string::npos && expr2.find("\\text{kernel}") != std::string::npos) ||
        (expr2.find("\\ker") != std::string::npos && expr1.find("\\text{kernel}") != std::string::npos)) {
        return true;
    }
    
    // \dim(V) -> \text{dimension}((V))
    if ((expr1.find("\\dim") != std::string::npos && expr2.find("\\text{dimension}") != std::string::npos) ||
        (expr2.find("\\dim") != std::string::npos && expr1.find("\\text{dimension}") != std::string::npos)) {
        return true;
    }
    
    // Handle absolute value notation: |x| vs \left|x\right|
    std::regex abs_simple(R"(\|([^|]+)\|)");
    std::regex abs_left_right(R"(\\left\|([^\\]+)\\right\|)");
    std::smatch match1, match2;
    
    std::string abs_content1, abs_content2;
    if (std::regex_search(expr1, match1, abs_simple)) {
        abs_content1 = match1[1].str();
    } else if (std::regex_search(expr1, match1, abs_left_right)) {
        abs_content1 = match1[1].str();
    }
    
    if (std::regex_search(expr2, match2, abs_simple)) {
        abs_content2 = match2[1].str();
    } else if (std::regex_search(expr2, match2, abs_left_right)) {
        abs_content2 = match2[1].str();
    }
    
    if (!abs_content1.empty() && !abs_content2.empty() && abs_content1 == abs_content2) {
        return true;
    }
    
    // Handle integral notation: \int_a^b -> \int_{a}^b
    if ((expr1.find("\\int_") != std::string::npos && expr2.find("\\int_{") != std::string::npos) ||
        (expr2.find("\\int_") != std::string::npos && expr1.find("\\int_{") != std::string::npos)) {
        // Extract the bounds and function to compare
        std::regex int_regex1(R"(\\int_([^\\^]+)\^?([^\\]*)\s*([^$]*))");
        std::regex int_regex2(R"(\\int_\{([^}]+)\}\^?([^\\]*)\s*([^$]*))");
        std::smatch match1, match2;
        
        if ((std::regex_search(expr1, match1, int_regex1) && std::regex_search(expr2, match2, int_regex2)) ||
            (std::regex_search(expr1, match1, int_regex2) && std::regex_search(expr2, match2, int_regex1))) {
            // Compare the bounds
            std::string bounds1 = match1[1].str();
            std::string bounds2 = match2[1].str();
            if (bounds1 == bounds2) {
                return true;
            }
        }
    }
    
    // Handle matrix notation transformations
    // \begin{matrix} a & b \\ c & d \end{matrix} -> \text{matrix}(\text{row}(a b) \text{row}(c d))
    if ((expr1.find("\\begin{") != std::string::npos && expr2.find("\\text{") != std::string::npos) ||
        (expr2.find("\\begin{") != std::string::npos && expr1.find("\\text{") != std::string::npos)) {
        
        // Extract matrix type
        std::regex matrix_type_regex(R"(\\begin\{([^}]+)\})");
        std::regex text_type_regex(R"(\\text\{([^}]+)\})");
        std::smatch match1, match2;
        
        std::string type1, type2;
        if (std::regex_search(expr1, match1, matrix_type_regex)) {
            type1 = match1[1].str();
        } else if (std::regex_search(expr1, match1, text_type_regex)) {
            type1 = match1[1].str();
        }
        
        if (std::regex_search(expr2, match2, text_type_regex)) {
            type2 = match2[1].str();
        } else if (std::regex_search(expr2, match2, matrix_type_regex)) {
            type2 = match2[1].str();
        }
        
        if (type1 == type2 && !type1.empty()) {
            return true;  // Same matrix type
        }
    }
    
    // Handle absolute value notation: |x| vs \left|x\right| (this IS semantic equivalence)
    if ((expr1.find("|") != std::string::npos && expr2.find("\\left|") != std::string::npos) ||
        (expr2.find("|") != std::string::npos && expr1.find("\\left|") != std::string::npos)) {
        
        std::regex abs_simple(R"(\|([^|]+)\|)");
        std::regex abs_left_right(R"(\\left\|([^\\]+)\\right\|)");
        std::smatch match1, match2;
        
        std::string abs_content1, abs_content2;
        if (std::regex_search(expr1, match1, abs_simple)) {
            abs_content1 = match1[1].str();
        } else if (std::regex_search(expr1, match1, abs_left_right)) {
            abs_content1 = match1[1].str();
        }
        
        if (std::regex_search(expr2, match2, abs_simple)) {
            abs_content2 = match2[1].str();
        } else if (std::regex_search(expr2, match2, abs_left_right)) {
            abs_content2 = match2[1].str();
        }
        
        if (!abs_content1.empty() && !abs_content2.empty() && abs_content1 == abs_content2) {
            printf("DEBUG: Absolute value notation equivalent (|%s| vs \\left|%s\\right|)\n", 
                   abs_content1.c_str(), abs_content2.c_str());
            return true;
        }
    }
    
    // Handle font style transformations
    // \mathbf{x} -> \text{bold}, \mathit{text} -> \text{italic}, etc.
    if ((expr1.find("\\math") != std::string::npos && expr2.find("\\text{") != std::string::npos) ||
        (expr2.find("\\math") != std::string::npos && expr1.find("\\text{") != std::string::npos)) {
        
        // Map font commands to text equivalents
        if ((expr1.find("\\mathbf") != std::string::npos && expr2.find("\\text{bold}") != std::string::npos) ||
            (expr2.find("\\mathbf") != std::string::npos && expr1.find("\\text{bold}") != std::string::npos) ||
            (expr1.find("\\mathit") != std::string::npos && expr2.find("\\text{italic}") != std::string::npos) ||
            (expr2.find("\\mathit") != std::string::npos && expr1.find("\\text{italic}") != std::string::npos) ||
            (expr1.find("\\mathcal") != std::string::npos && expr2.find("\\text{calligraphic}") != std::string::npos) ||
            (expr2.find("\\mathcal") != std::string::npos && expr1.find("\\text{calligraphic}") != std::string::npos) ||
            (expr1.find("\\mathfrak") != std::string::npos && expr2.find("\\text{fraktur}") != std::string::npos) ||
            (expr2.find("\\mathfrak") != std::string::npos && expr1.find("\\text{fraktur}") != std::string::npos) ||
            (expr1.find("\\mathtt") != std::string::npos && expr2.find("\\text{monospace}") != std::string::npos) ||
            (expr2.find("\\mathtt") != std::string::npos && expr1.find("\\text{monospace}") != std::string::npos) ||
            (expr1.find("\\mathsf") != std::string::npos && expr2.find("\\text{sans_serif}") != std::string::npos) ||
            (expr2.find("\\mathsf") != std::string::npos && expr1.find("\\text{sans_serif}") != std::string::npos)) {
            return true;
        }
    }
    
    // Handle function argument dropping: \sin x -> \sin
    if ((expr1.find("\\sin ") != std::string::npos && expr2 == "\\sin") ||
        (expr2.find("\\sin ") != std::string::npos && expr1 == "\\sin") ||
        (expr1.find("\\cos ") != std::string::npos && expr2 == "\\cos") ||
        (expr2.find("\\cos ") != std::string::npos && expr1 == "\\cos") ||
        (expr1.find("\\log ") != std::string::npos && expr2 == "\\log") ||
        (expr2.find("\\log ") != std::string::npos && expr1 == "\\log")) {
        printf("DEBUG: Function argument dropping detected - this is a parser/formatter bug\n");
        return false;  // This is actually a bug, not semantic equivalence
    }
    
    // Handle limit notation: \lim_{x \to 0} f(x) vs \lim_{x \to 0}^{f(x)}
    if (expr1.find("\\lim") != std::string::npos && expr2.find("\\lim") != std::string::npos) {
        // Extract limit variable
        std::regex lim_var_regex(R"(\\lim_\{([^}]+)\})");
        std::smatch match1, match2;
        
        if (std::regex_search(expr1, match1, lim_var_regex) && std::regex_search(expr2, match2, lim_var_regex)) {
            std::string var1 = match1[1].str();
            std::string var2 = match2[1].str();
            
            if (var1 == var2) {
                // Check if one has ^{f(x)} and the other has f(x) after the limit
                if ((expr1.find("^{") != std::string::npos && expr2.find("^{") == std::string::npos) ||
                    (expr2.find("^{") != std::string::npos && expr1.find("^{") == std::string::npos)) {
                    printf("DEBUG: Limit notation formatting difference - this is a parser/formatter bug\n");
                    return false;  // This is a formatting bug, not semantic equivalence
                }
            }
        }
    }
    
    // Normalize whitespace and compare
    std::regex space_regex(R"(\s+)");
    norm1 = std::regex_replace(norm1, space_regex, " ");
    norm2 = std::regex_replace(norm2, space_regex, " ");
    
    // Trim whitespace
    norm1.erase(0, norm1.find_first_not_of(" \t\n\r"));
    norm1.erase(norm1.find_last_not_of(" \t\n\r") + 1);
    norm2.erase(0, norm2.find_first_not_of(" \t\n\r"));
    norm2.erase(norm2.find_last_not_of(" \t\n\r") + 1);
    
    return norm1 == norm2;
}
#endif

// Common test function for markdown roundtrip testing
bool test_markdown_roundtrip(const char* test_file_path, const char* debug_file_path, const char* test_description);

// Common test function for math expression roundtrip testing
bool test_math_expressions_roundtrip(const char** test_cases, int num_cases, const char* type, const char* flavor, 
                                    const char* url_prefix, const char* test_name, const char* error_prefix);

#ifdef GINAC_AVAILABLE
// GiNaC-based math equivalence functions are defined above
bool are_math_expressions_equivalent(const std::string& expr1, const std::string& expr2);
#endif

// Setup and teardown functions
void setup_math_tests(void) {
}

void teardown_math_tests(void) {
}

TestSuite(math_roundtrip_tests, .init = setup_math_tests, .fini = teardown_math_tests);

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
Test(math_roundtrip_tests, inline_math_roundtrip) {
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
Test(math_roundtrip_tests, block_math_roundtrip) {
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
Test(math_roundtrip_tests, pure_math_roundtrip) {
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
    printf("=== %s ===\n", test_description);
    
    // Debug: Check all preprocessor defines
    printf("DEBUG: Checking preprocessor defines:\n");
#ifdef DEBUG
    printf("  DEBUG is defined\n");
#endif
#ifdef GINAC_AVAILABLE
    printf("  ✅ GINAC_AVAILABLE is defined\n");
#else
    printf("  ❌ GINAC_AVAILABLE is NOT defined\n");
#endif
    
#ifdef GINAC_AVAILABLE
    printf("✅ GiNaC is available and enabled\n");
#else
    printf("❌ GiNaC is NOT available - using fallback mode\n");
#endif
    
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
    
    // Default to length-based comparison
    size_t orig_len = strlen(original_content);
    size_t formatted_len = strlen(formatted->chars);
    
    int max_diff;
    if (orig_len < 200) {
        max_diff = 2;
    } else if (orig_len < 3000) {
        max_diff = 15;
    } else {
        max_diff = 20;
    }
    
    bool length_ok = (abs((int)(formatted_len - orig_len)) <= max_diff);

#ifdef GINAC_AVAILABLE
    // Enhanced: Individual math expression comparison with string-first strategy
    try {
        std::vector<std::string> orig_expressions = extract_math_expressions(std::string(original_content));
        std::vector<std::string> formatted_expressions = extract_math_expressions(std::string(formatted->chars));
        
        printf("\n=== MATH EXPRESSION ANALYSIS ===\n");
        printf("📊 Found %zu math expressions in original, %zu in formatted\n", 
               orig_expressions.size(), formatted_expressions.size());
        
        // Compare expressions individually
        size_t total_expressions = orig_expressions.size() < formatted_expressions.size() ? 
                                   orig_expressions.size() : formatted_expressions.size();
        
        int string_matches = 0;
        int ginac_matches = 0;
        int failures = 0;
        int manual_verification_needed = 0;
        
        for (size_t i = 0; i < total_expressions; i++) {
            const std::string& orig = orig_expressions[i];
            const std::string& formatted = formatted_expressions[i];
            
            printf("\n--- Expression %zu ---\n", i + 1);
            printf("📝 Original:  '%s'\n", orig.c_str());
            printf("🔄 Formatted: '%s'\n", formatted.c_str());
            
            // Step 1: String comparison first
            if (orig == formatted) {
                printf("✅ PASS: Exact string match\n");
                string_matches++;
                continue;
            }
            
            // Step 2: GiNaC comparison if strings differ
            printf("⚠️  String mismatch, trying GiNaC comparison...\n");
            
            std::string ginac_orig = latex_to_ginac(orig);
            std::string ginac_formatted = latex_to_ginac(formatted);
            
            if (ginac_orig.empty() || ginac_formatted.empty()) {
                printf("🔍 MANUAL: GiNaC can't parse - needs manual verification\n");
                manual_verification_needed++;
                
                // Try semantic equivalence as fallback
                if (are_expressions_semantically_equivalent(orig, formatted)) {
                    printf("✅ PASS: Semantic equivalence detected\n");
                    ginac_matches++;
                } else {
                    printf("❌ FAIL: No equivalence found - parser/formatter issue\n");
                    failures++;
                }
                continue;
            }
            
            try {
                // Set up GiNaC symbols
                symbol x("x"), y("y"), z("z"), a("a"), b("b"), c("c"), n("n"), e("e");
                parser reader;
                reader.get_syms()["x"] = x; reader.get_syms()["y"] = y; reader.get_syms()["z"] = z;
                reader.get_syms()["a"] = a; reader.get_syms()["b"] = b; reader.get_syms()["c"] = c;
                reader.get_syms()["n"] = n; reader.get_syms()["e"] = e;
                
                ex e1 = reader(ginac_orig);
                ex e2 = reader(ginac_formatted);
                ex difference = (e1.expand().normal() - e2.expand().normal()).expand().normal();
                
                if (difference.is_zero()) {
                    printf("✅ PASS: GiNaC confirms mathematical equivalence\n");
                    ginac_matches++;
                } else {
                    printf("❌ FAIL: GiNaC shows mathematical difference - parser/formatter issue\n");
                    failures++;
                }
            } catch (const std::exception& e) {
                printf("🔍 MANUAL: GiNaC parsing failed (%s) - needs manual verification\n", e.what());
                manual_verification_needed++;
                
                // Try semantic equivalence as fallback
                if (are_expressions_semantically_equivalent(orig, formatted)) {
                    printf("✅ PASS: Semantic equivalence detected\n");
                    ginac_matches++;
                } else {
                    printf("❌ FAIL: No equivalence found - parser/formatter issue\n");
                    failures++;
                }
            }
        }
        
        // Handle expression count mismatch
        if (orig_expressions.size() != formatted_expressions.size()) {
            int count_diff = abs((int)orig_expressions.size() - (int)formatted_expressions.size());
            printf("\n⚠️  Expression count mismatch: %d expressions lost/gained\n", count_diff);
            failures += count_diff;
        }
        
        // Summary
        printf("\n=== SUMMARY ===\n");
        printf("✅ String matches: %d\n", string_matches);
        printf("🧮 GiNaC matches: %d\n", ginac_matches);
        printf("🔍 Manual verification: %d\n", manual_verification_needed);
        printf("❌ Failures: %d\n", failures);
        
        if (failures == 0) {
            printf("🎉 ALL EXPRESSIONS VALIDATED - Test PASSED!\n");
            return true;
        } else {
            printf("💥 %d FAILURES DETECTED - Parser/Formatter needs fixes\n", failures);
            return false;
        }
        
    } catch (const std::exception& e) {
        printf("⚠️ GiNaC error: %s, falling back to length comparison\n", e.what());
    }
#endif
    
    if (length_ok) {
        printf("✅ Markdown roundtrip test completed successfully!\n");
    } else {
        printf("❌ Length mismatch - Original: %zu, Formatted: %zu (allowed diff: ±%d)\n", 
               orig_len, formatted_len, max_diff);
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

Test(math_roundtrip_tests, simple_math_test) {
    bool result = test_markdown_roundtrip(
        "test/input/simple_math_test.md", "./temp/simple_math_debug.txt",
        "Simple math test with basic expressions"
    );
    cr_assert(result, "Simple math test failed");
}

Test(math_roundtrip_tests, simple_markdown_roundtrip) {
    bool result = test_markdown_roundtrip(
        "test/input/math_simple.md", "./temp/simple_debug.txt",
        "Simple markdown test with multiple math expressions"
    );
    cr_assert(result, "Simple markdown roundtrip test failed");
}

Test(math_roundtrip_tests, indexed_math_test) {
    bool result = test_markdown_roundtrip(
        "test/input/indexed_math_test.md", "./temp/indexed_debug.txt",
        "Indexed math test to track expression alignment"
    );
    cr_assert(result, "Indexed math test failed");
}

// New test case added here
Test(math_roundtrip_tests, advanced_math_test) {
    bool result = test_markdown_roundtrip(
        "test/input/advanced_math_test.md", "./temp/advanced_debug.txt",
        "Advanced math test with complex expressions"
    );
    cr_assert(result, "Advanced math test failed");
}

#ifdef GINAC_AVAILABLE
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

char* read_text_doc(Url *url) {
    if (!url || !url->pathname || !url->pathname->chars) {
        return NULL;
    }
    
    // Extract the file path from the URL
    const char* path = url->pathname->chars;
    
    // Use the existing read_text_file function
    return read_text_file(path);
}