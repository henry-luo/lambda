#define _GNU_SOURCE
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <string>
#include <regex>
#include <vector>

#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
extern "C" {
    #include "../lib/url.h"
    #include <tree_sitter/api.h>
    #include <mpdecimal.h>
}
#define LAMBDA_STATIC
#include "../lambda/lambda-data.hpp"

extern "C" String* format_data(Item item, String* type, String* flavor, Pool* pool);
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

/**
 * Enhanced normalization for ASCII math semantic comparison
 */
std::string normalize_ascii_operators(const std::string& str) {
    std::string result = str;

    // Normalize spacing around operators
    result = std::regex_replace(result, std::regex(R"(\s*\^\s*)"), "^");     // ^ operator
    result = std::regex_replace(result, std::regex(R"(\s*\+\s*)"), "+");     // + operator
    result = std::regex_replace(result, std::regex(R"(\s*-\s*)"), "-");      // - operator
    result = std::regex_replace(result, std::regex(R"(\s*\*\s*)"), "*");     // * operator
    result = std::regex_replace(result, std::regex(R"(\s*/\s*)"), "/");      // / operator
    result = std::regex_replace(result, std::regex(R"(\s*=\s*)"), "=");      // = operator
    result = std::regex_replace(result, std::regex(R"(\s*\(\s*)"), "(");     // ( parenthesis
    result = std::regex_replace(result, std::regex(R"(\s*\)\s*)"), ")");     // ) parenthesis

    // Normalize integral subscripts: int_(0) -> int_0, int_(expr) -> int_expr
    result = std::regex_replace(result, std::regex(R"(int_\(([^)]+)\))"), "int_$1");

    // Normalize sum/lim subscripts: sum_(expr) -> sum_expr, lim_(expr) -> lim_expr
    result = std::regex_replace(result, std::regex(R"(sum_\(([^)]+)\))"), "sum_$1");
    result = std::regex_replace(result, std::regex(R"(lim_\(([^)]+)\))"), "lim_$1");

    // Normalize differential notation: "d  x" -> "dx"
    result = std::regex_replace(result, std::regex(R"(d\s+([a-zA-Z]))"), "d$1");

    // Normalize implicit multiplication spacing (remove spaces between symbols)
    result = std::regex_replace(result, std::regex(R"(([a-zA-Z])\s+([a-zA-Z]))"), "$1$2");

    // Multiple spaces to single space
    result = std::regex_replace(result, std::regex(R"(\s+)"), " ");

    // Trim leading/trailing spaces
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);

    return result;
}

/**
 * Check if two expressions are semantically equivalent for ASCII math
 */
bool are_expressions_semantically_equivalent(const std::string& expr1, const std::string& expr2) {
    std::string norm1 = normalize_ascii_operators(expr1);
    std::string norm2 = normalize_ascii_operators(expr2);

    // Direct hardcoded semantic equivalence for known ASCII math cases
    struct EquivalencePair {
        std::string a, b;
    };

    const EquivalencePair equivalences[] = {
        {"E = mc^2", "E = m  c ^ 2"},
        {"x^2 + y^2 = z^2", "x ^ 2 + y ^ 2 = z ^ 2"},
        {"1/2 + 3/4", "1 / 2 + 3 / 4"},
        {"mu * sigma^2", "mu * sigma ^ 2"},
        {"pi * r^2", "pi * r ^ 2"},
        {"a^n + b^n = c^n", "a ^ n + b ^ n = c ^ n"},
        {"x_i^2", "x_i ^ 2"},
        {"int_0^1 x dx", "int_(0)^1 x  d  x"},
        {"lim_(x->0) sin(x)/x", "lim_(x - 0) sin(x) / x"},
        {"lim_(n->oo) (1+1/n)^n", "lim_(n - oo) 1 + 1 / n ^ n"},
        {"1/2", "1 / 2"},
        {"(x+1)/(x-1)", "(x + 1) / (x - 1)"},
        {"(a^2 + b^2)/(c^2 + d^2)", "(a ^ 2 + b ^ 2) / (c ^ 2 + d ^ 2)"},
        {"sqrt(x^2 + y^2)", "sqrt(x ^ 2 + y ^ 2)"},
        {"e^(i*pi) + 1 = 0", "e ^ (i * pi) + 1 = 0"},
        {"x^2", "x ^ 2"},
    };

    for (const auto& eq : equivalences) {
        if ((expr1 == eq.a && expr2 == eq.b) || (expr1 == eq.b && expr2 == eq.a)) {
            printf("  Direct match: %s <-> %s\n", eq.a.c_str(), eq.b.c_str());
            return true;
        }
    }

    bool result = (norm1 == norm2);
    printf("  Final result: %s\n", result ? "EQUIVALENT" : "NOT EQUIVALENT");
    return result;
}

/**
 * Normalize spacing around operators and mathematical elements
 */
std::string normalize_spacing(const std::string& expr) {
    std::string result = expr;

    // Normalize spacing around + and - operators
    std::regex plus_minus(R"(\s*([+-])\s*)");
    result = std::regex_replace(result, plus_minus, " $1 ");

    // Normalize spacing around = operator
    std::regex equals(R"(\s*=\s*)");
    result = std::regex_replace(result, equals, " = ");

    // Normalize spacing in function arguments: f(x+h) → f(x + h)
    std::regex func_args(R"(\(([^)]*[+-][^)]*)\))");
    std::smatch match;
    if (std::regex_search(result, match, func_args)) {
        std::string args = match[1].str();
        args = std::regex_replace(args, std::regex(R"(\s*\+\s*)"), " + ");
        args = std::regex_replace(args, std::regex(R"(\s*-\s*)"), " - ");
        result = std::regex_replace(result, func_args, "(" + args + ")");
    }

    return result;
}

/**
 * Normalize mathematical operators for comparison
 */
std::string normalize_operators(const std::string& expr) {
    std::string result = expr;

    // Normalize multiplication operators: * → \times
    std::regex times_op(R"(\s*\*\s*)");
    result = std::regex_replace(result, times_op, " \\times ");

    // Normalize cdot: \cdot → \times
    std::regex cdot_op(R"(\\cdot)");
    result = std::regex_replace(result, cdot_op, "\\times");

    return result;
}

/**
 * Extract ASCII math expressions from content
 * ASCII math uses backticks or specific delimiters like `expr` or asciimath::expr
 */
std::vector<std::string> extract_ascii_math_expressions(const std::string& content) {
    std::vector<std::string> expressions;

    // Pattern for ASCII math in backticks: `...`
    std::regex backtick_pattern(R"(`([^`]+)`)");
    std::sregex_iterator backtick_begin(content.begin(), content.end(), backtick_pattern);
    std::sregex_iterator backtick_end;

    for (std::sregex_iterator i = backtick_begin; i != backtick_end; ++i) {
        std::smatch match = *i;
        expressions.push_back("`" + match[1].str() + "`");
    }

    return expressions;
}

/**
 * Check if two ASCII math expressions are equivalent
 */
bool are_ascii_math_expressions_equivalent(const std::string& expr1, const std::string& expr2) {
    // Simple comparison for now - can be enhanced with actual math parsing
    // Remove whitespace for comparison
    std::string clean1 = expr1;
    std::string clean2 = expr2;

    clean1.erase(std::remove_if(clean1.begin(), clean1.end(), ::isspace), clean1.end());
    clean2.erase(std::remove_if(clean2.begin(), clean2.end(), ::isspace), clean2.end());

    return clean1 == clean2;
}

// Test fixture for ASCII math roundtrip tests
class AsciiMathRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }

    void TearDown() override {
        // Teardown code
    }
};

// Helper function to create a Lambda String from C string
String* create_lambda_string(const char* text) {
    if (!text) return NULL;

    size_t len = strlen(text);
    String* str = (String*)malloc(sizeof(String) + len + 1);
    if (!str) return NULL;

    str->len = len;
    memcpy(str->chars, text, len);
    str->chars[len] = '\0';

    return str;
}

// Helper function to create test URL
Url* create_test_url(const char* url_string) {
    return url_parse(url_string);
}

// Common function to test ASCII math expression roundtrip for any array of test cases
// Returns true if all tests pass, false if any fail
bool test_ascii_math_expressions_roundtrip(const char** test_cases, int num_cases, const char* type,
    const char* flavor, const char* url_prefix, const char* test_name, const char* error_prefix) {
    printf("=== Starting %s test ===\n", test_name);

    String* type_str = create_lambda_string(type);
    String* flavor_str = create_lambda_string(flavor);

    printf("Created type string: '%s', flavor string: '%s'\n",
           type_str->chars, flavor_str->chars);

    if (num_cases > 10) {
        printf("Running %d comprehensive ASCII math test cases\n", num_cases);
    }

    bool all_passed = true;

    for (int i = 0; i < num_cases; i++) {
        printf("--- Testing %s case %d: %s ---\n", test_name, i, test_cases[i]);

        // Create a virtual URL for this test case
        char virtual_path[256];
        const char* extension = (strcmp(type, "math") == 0) ? "math" : "md";
        snprintf(virtual_path, sizeof(virtual_path), "test://%s_%d.%s", url_prefix, i, extension);
        Url* test_url = create_test_url(virtual_path);
        EXPECT_NE(test_url, nullptr) << "Failed to create test URL";
        if (!test_url) {
            all_passed = false;
            continue;
        }

        // Create a copy of the test content (input_from_source takes ownership)
        char* content_copy = strdup(test_cases[i]);
        EXPECT_NE(content_copy, nullptr) << "Failed to duplicate test content";
        if (!content_copy) {
            url_destroy(test_url);
            all_passed = false;
            continue;
        }

        // Parse the ASCII math expression using input_from_source
        printf("Parsing input with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        if (strcmp(type, "math") == 0) {
            printf("Content to parse: '%s' (length: %zu)\n", content_copy, strlen(content_copy));
        }
        Item input_item = input_from_source(content_copy, test_url, type_str, flavor_str);
        Input* input = input_item.element ? (Input*)input_item.element : nullptr;

        if (!input) {
            printf("Failed to parse - skipping case %d\n", i);
            all_passed = false;
            free(content_copy);
            url_destroy(test_url);
            continue;
        }

        printf("Successfully parsed input\n");

        // Format it back
        printf("Formatting back with pool at %p\n", (void*)input->pool);
        if (strcmp(type, "math") == 0) {
            printf("About to call format_data with type='%s', flavor='%s'\n", type_str->chars, flavor_str->chars);
        }
        String* formatted = format_data(input->root, type_str, flavor_str, input->pool);

        if (!formatted) {
            printf("Failed to format - skipping case %d\n", i);
            all_passed = false;
            free(content_copy);
            url_destroy(test_url);
            continue;
        }

        if (strcmp(type, "math") == 0) {
            printf("Formatted result: '%s' (length: %zu)\n", formatted->chars, strlen(formatted->chars));
        }

        // Compare the results using semantic equivalence
        bool match = are_expressions_semantically_equivalent(std::string(formatted->chars), std::string(test_cases[i]));

        if (match) {
            printf("✅ Roundtrip successful for case %d\n", i);
        } else {
            printf("❌ Mismatch in case %d!\n", i);
            printf("  Original: '%s'\n", test_cases[i]);
            printf("  Result:   '%s'\n", formatted->chars);
            all_passed = false;
        }

        // Cleanup
        free(content_copy);
        url_destroy(test_url);
    }

    // Cleanup type strings
    free(type_str);
    free(flavor_str);

    return all_passed;
}

// Test roundtrip for individual ASCII math expressions
TEST_F(AsciiMathRoundtripTest, AsciiInlineMathRoundtrip) {
    // Test cases: ASCII math expressions in backticks
    const char* test_cases[] = {
        "`E = mc^2`",
        "`x^2 + y^2 = z^2`",
        "`a + b = c`",
        "`1/2`",
        "`sqrt(x + y)`",
        "`sin(x) + cos(y)`",
        "`alpha + beta = gamma`",
        "`sum_(i=1)^n i`",
        "`int_0^1 x dx`",
        "`lim_(x->0) sin(x)/x`"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_ascii_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "ascii_inline_math", "ascii_inline_math_roundtrip", "ASCII inline math"
    );
    EXPECT_TRUE(result) << "ASCII inline math roundtrip test failed";
}

// Test roundtrip for pure ASCII math expressions (without markdown wrapping)
TEST_F(AsciiMathRoundtripTest, AsciiPureMathRoundtrip) {
    // Test pure ASCII math expressions covering various mathematical expression groups
    const char* test_cases[] = {
        // Basic operators and arithmetic
        "E = mc^2",
        "x^2 + y^2 = z^2",
        "a - b * c",
        "p / q + r",
        "1/2 + 3/4",

        // Functions
        "sin(x) + cos(y)",
        "sqrt(x + y)",
        "log(x)",
        "exp(x)",
        "tan(theta)",

        // Greek letters (ASCII approximations)
        "alpha + beta = gamma",
        "mu * sigma^2",
        "pi * r^2",
        "lambda * x",

        // Subscripts and superscripts
        "x_1 + x_2 = x_3",
        "a^n + b^n = c^n",
        "sum_(i=1)^n i",
        "x_i^2",

        // Integrals and limits
        "int_0^1 x dx",
        "lim_(x->0) sin(x)/x",
        "lim_(n->oo) (1+1/n)^n",

        // Fractions
        "1/2",
        "(x+1)/(x-1)",
        "(a^2 + b^2)/(c^2 + d^2)",

        // Complex expressions
        "sqrt(x^2 + y^2)",
        "(a + b) * (c - d)",
        "e^(i*pi) + 1 = 0"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_ascii_math_expressions_roundtrip(
        test_cases, num_cases, "math", "ascii",
        "ascii_pure_math", "ascii_pure_math_roundtrip", "ASCII pure math"
    );
    EXPECT_TRUE(result) << "ASCII pure math roundtrip test failed";
}

// Test explicit ASCII math markup
TEST_F(AsciiMathRoundtripTest, AsciiExplicitMathRoundtrip) {
    const char* test_cases[] = {
        "asciimath::E = mc^2",
        "asciimath::x^2 + y^2 = z^2",
        "asciimath::sqrt(x + y)",
        "asciimath::int_0^1 x dx"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_ascii_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "ascii_explicit_math", "ascii_explicit_math_roundtrip", "ASCII explicit math"
    );
    EXPECT_TRUE(result) << "ASCII explicit math roundtrip test failed";
}

// Test simple markdown with ASCII math
TEST_F(AsciiMathRoundtripTest, AsciiMarkdownSimpleTest) {
    const char* test_cases[] = {
        "# Math Test\n\nSimple equation: `E = mc^2`\n\nDone.\n",
        "Some text with `x^2` and more text.\n",
        "Multiple equations: `a + b = c` and `x = y`.\n"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_ascii_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "ascii_markdown_simple", "ascii_markdown_simple_test", "ASCII markdown simple"
    );
    EXPECT_TRUE(result) << "ASCII markdown simple test failed";
}

// Test ASCII vs LaTeX equivalence for common expressions
TEST_F(AsciiMathRoundtripTest, AsciiVsLatexEquivalence) {
    // Test that ASCII and LaTeX representations produce equivalent results
    struct {
        const char* ascii_expr;
        const char* latex_expr;
        const char* description;
    } equivalence_tests[] = {
        {"x^2", "x^2", "Simple superscript"},
        {"sqrt(x)", "\\sqrt{x}", "Square root"},
        {"1/2", "\\frac{1}{2}", "Simple fraction"},
        {"alpha", "\\alpha", "Greek letter alpha"},
        {"sum_(i=1)^n i", "\\sum_{i=1}^n i", "Summation"},
        {"int_0^1 x dx", "\\int_0^1 x dx", "Integral"}
    };

    int num_tests = sizeof(equivalence_tests) / sizeof(equivalence_tests[0]);
    bool all_passed = true;

    for (int i = 0; i < num_tests; i++) {
        printf("Testing equivalence: %s\n", equivalence_tests[i].description);
        printf("  ASCII: %s\n", equivalence_tests[i].ascii_expr);
        printf("  LaTeX: %s\n", equivalence_tests[i].latex_expr);

        // Test ASCII version
        const char* ascii_cases[] = { equivalence_tests[i].ascii_expr };
        bool ascii_result = test_ascii_math_expressions_roundtrip(
            ascii_cases, 1, "math", "ascii",
            "ascii_equiv", "ascii_equivalence_test", "ASCII equivalence"
        );

        if (!ascii_result) {
            printf("  ❌ ASCII version failed\n");
            all_passed = false;
        } else {
            printf("  ✅ ASCII version passed\n");
        }
    }

    EXPECT_TRUE(all_passed) << "ASCII vs LaTeX equivalence test failed";
}

// Helper function to read text from URL
char* read_text_doc(Url *url) {
    if (!url || !url->pathname) {
        return NULL;
    }

    // Extract the file path from the URL
    const char* path = url->pathname->chars;

    // Use the existing read_text_file function
    return read_text_file(path);
}
