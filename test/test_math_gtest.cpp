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
#include "../lib/mempool.h"
#include "../lib/log.h"
extern "C" {
    #include "../lib/url.h"  // Use new URL parser
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
 * Normalize spacing around operators and mathematical elements
 */
std::string normalize_spacing(const std::string& expr) {
    std::string result = expr;

    // Handle subscript content with equals signs (like n=1) - normalize spacing in subscripts
    std::regex subscript_equals(R"(_\{\s*([^}]*?)\s*=\s*([^}]*?)\s*\})");
    result = std::regex_replace(result, subscript_equals, "_{$1=$2}");

    // Handle \quad spacing carefully - normalize \quad with single spaces
    // First, normalize any \quad that doesn't already have proper spacing
    std::regex quad_normalize(R"(\\quad\s*([a-zA-Z0-9]))");
    result = std::regex_replace(result, quad_normalize, "\\quad $1");

    std::regex quad_before(R"(([a-zA-Z0-9])\s*\\quad)");
    result = std::regex_replace(result, quad_before, "$1 \\quad");

    // For expressions like "x \quad y", preserve the space before y
    std::regex quad_expression(R"(\\quad\s+([a-zA-Z0-9])\s*\\quad\s*([a-zA-Z0-9]))");
    result = std::regex_replace(result, quad_expression, "\\quad $1 \\quad $2");

    // Normalize spacing around + and - operators (but not in subscripts/superscripts)
    std::regex plus_minus(R"(\s*([+-])\s*)");
    result = std::regex_replace(result, plus_minus, " $1 ");

    // Normalize spacing around = operator (but not in subscripts - those are already handled)
    std::regex equals(R"((?<!_\{[^}]*)\s*=\s*(?![^}]*\}))");
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
 * Normalize LaTeX expression for semantic comparison:
 * - Collapse multiple spaces to single space
 * - Normalize spacing around operators
 */
std::string normalize_latex_for_comparison(const std::string& expr) {
    std::string result;
    result.reserve(expr.size());
    bool in_command = false;
    bool in_subscript = false;
    bool in_superscript = false;

    for (size_t i = 0; i < expr.size(); i++) {
        char c = expr[i];

        if (c == '\\') {
            in_command = true;
            result += c;
        } else if (c == '_') {
            in_subscript = true;
            in_superscript = false;
            result += c;
        } else if (c == '^') {
            in_superscript = true;
            in_subscript = false;
            result += c;
        } else if (in_command && !std::isalpha(c)) {
            in_command = false;
            // Skip spaces between command and non-letter if next char is {
            if (c == ' ') {
                // Skip trailing spaces after command before brace
                while (i + 1 < expr.size() && expr[i + 1] == ' ') {
                    i++;
                }
            }
            result += c;
        } else if (c == ' ') {
            // Remove spaces around = in subscripts/superscripts
            if ((in_subscript || in_superscript) && (result.back() == '=' || (i + 1 < expr.size() && expr[i + 1] == '='))) {
                // Skip space around equals
                continue;
            }
            // Remove space after single-token subscript/superscript (ends the script)
            if (in_subscript || in_superscript) {
                // Space signals end of non-braced script content - skip it
                in_subscript = false;
                in_superscript = false;
                continue;
            }
            // Collapse multiple spaces to one
            while (i + 1 < expr.size() && expr[i + 1] == ' ') {
                i++;
            }
            result += c;
        } else if (c == '{') {
            // Check if this is an optional brace around single char/command in sub/sup
            if ((in_subscript || in_superscript) && i + 2 < expr.size()) {
                // Look for single char or command
                size_t closing = i + 1;
                if (expr[closing] == '\\') {
                    // It's a command - find end of command
                    closing++;
                    while (closing < expr.size() && std::isalpha(expr[closing])) {
                        closing++;
                    }
                } else {
                    // Single char
                    closing++;
                }

                if (closing < expr.size() && expr[closing] == '}') {
                    // Found matching brace - skip braces
                    i++; // skip {
                    while (i < closing) {
                        result += expr[i];
                        i++;
                    }
                    // i now points to }, will be incremented in loop
                    in_subscript = false;
                    in_superscript = false;
                    continue;
                }
            }
            result += c;
        } else {
            result += c;
            // Reset script mode after non-brace character (unless it's part of command)
            if (!in_command && c != '=' && !std::isalnum(c)) {
                in_subscript = false;
                in_superscript = false;
            }
        }
    }
    return result;
}

/**
 * Check semantic equivalence for expressions that GiNaC can't parse
 */
// Normalize script braces: ^{x} and ^x are equivalent for single tokens
std::string normalize_script_braces(const std::string& expr) {
    std::string result;
    result.reserve(expr.size() * 2);

    for (size_t i = 0; i < expr.size(); i++) {
        char c = expr[i];

        // Check for ^ or _ followed by single token or already braced
        if ((c == '^' || c == '_') && i + 1 < expr.size()) {
            result += c;
            char next = expr[i + 1];

            if (next == '{') {
                // Already braced, keep as is
                result += next;
                i++;
            } else if (next == '\\') {
                // Command - add braces around it
                result += '{';
                result += next;
                i++;
                // Consume the command name
                while (i + 1 < expr.size() && std::isalpha(expr[i + 1])) {
                    result += expr[i + 1];
                    i++;
                }
                result += '}';
            } else if (std::isalnum(next)) {
                // Single character - add braces
                result += '{';
                result += next;
                result += '}';
                i++;
            } else {
                // Something else, keep as is
                result += next;
                i++;
            }
        } else {
            result += c;
        }
    }
    return result;
}

bool are_expressions_semantically_equivalent(const std::string& expr1, const std::string& expr2) {
    // Direct comparison first
    if (expr1 == expr2) {
        return true;
    }

    // Normalize and compare
    std::string norm1 = normalize_latex_for_comparison(expr1);
    std::string norm2 = normalize_latex_for_comparison(expr2);

    if (norm1 == norm2) {
        return true;
    }

    // Remove all spaces and compare (very lenient)
    std::string no_space1, no_space2;
    for (char c : expr1) if (c != ' ') no_space1 += c;
    for (char c : expr2) if (c != ' ') no_space2 += c;

    if (no_space1 == no_space2) {
        return true;
    }

    // Normalize script braces and compare without spaces
    std::string brace_norm1 = normalize_script_braces(no_space1);
    std::string brace_norm2 = normalize_script_braces(no_space2);

    if (brace_norm1 == brace_norm2) {
        return true;
    }

    // For any other case, return false (will cause mismatch)
    return false;
}

/**
 * Extract math expressions from markdown content
 */
std::vector<std::string> extract_math_expressions(const std::string& content) {
    std::vector<std::string> expressions;

    // Pattern for inline math: $...$
    std::regex inline_pattern(R"(\$([^$]+)\$)");
    std::sregex_iterator inline_begin(content.begin(), content.end(), inline_pattern);
    std::sregex_iterator inline_end;

    for (std::sregex_iterator i = inline_begin; i != inline_end; ++i) {
        std::smatch match = *i;
        expressions.push_back("$" + match[1].str() + "$");
    }

    // Pattern for block math: $$...$$
    std::regex block_pattern(R"(\$\$([^$]+)\$\$)", std::regex::multiline);
    std::sregex_iterator block_begin(content.begin(), content.end(), block_pattern);
    std::sregex_iterator block_end;

    for (std::sregex_iterator i = block_begin; i != block_end; ++i) {
        std::smatch match = *i;
        expressions.push_back("$$" + match[1].str() + "$$");
    }

    return expressions;
}

/**
 * Check if two math expressions are equivalent
 */
bool are_math_expressions_equivalent(const std::string& expr1, const std::string& expr2) {
    // Simple comparison for now - can be enhanced with actual math parsing
    // Remove whitespace for comparison
    std::string clean1 = expr1;
    std::string clean2 = expr2;

    clean1.erase(std::remove_if(clean1.begin(), clean1.end(), ::isspace), clean1.end());
    clean2.erase(std::remove_if(clean2.begin(), clean2.end(), ::isspace), clean2.end());

    return clean1 == clean2;
}

// Test fixture for math roundtrip tests
class MathRoundtripTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging
        log_init(NULL);
    }

    void TearDown() override {
        // Cleanup logging
        log_finish();
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

bool test_math_expressions_roundtrip(
    const char* test_cases[],
    int num_cases,
    const char* input_format,
    const char* input_flavor,
    const char* /* test_category */,  // Unused parameter
    const char* test_name,
    const char* description
) {
    printf("Testing %s: %s\n", description, test_name);

    bool all_passed = true;

    for (int i = 0; i < num_cases; i++) {
        const char* original = test_cases[i];
        printf("  Test case %d: %s\n", i + 1, original);

        // Create test URL
        Url* test_url = url_parse("test://memory");
        EXPECT_NE(test_url, nullptr) << "Failed to create test URL";
        if (!test_url) {
            all_passed = false;
            continue;
        }

        // Create content copy for parsing
        char* content_copy = strdup(original);
        EXPECT_NE(content_copy, nullptr) << "Failed to duplicate test content";
        if (!content_copy) {
            url_destroy(test_url);
            all_passed = false;
            continue;
        }

        String* input_type = create_lambda_string(input_format);
        String* input_flavor_str = create_lambda_string(input_flavor);

        // Parse the math expression
        Item parsed = input_from_source(content_copy, test_url, input_type, input_flavor_str);
        Input* input = parsed.element ? (Input*)parsed.element : nullptr;

        if (!input) {
            printf("    ❌ Failed to parse: %s\n", original);
            all_passed = false;
            free(content_copy);
            free(input_type);
            free(input_flavor_str);
            url_destroy(test_url);
            continue;
        }

        // Format back to the same format
        String* output_type = create_lambda_string(input_format);
        String* output_flavor = create_lambda_string(input_flavor);
        String* formatted = format_data(input->root, output_type, output_flavor, input->pool);

        if (!formatted) {
            printf("    ❌ Failed to format back: %s\n", original);
            all_passed = false;
        } else {
            // Compare the results using semantic equivalence
            bool match = are_expressions_semantically_equivalent(std::string(formatted->chars), std::string(test_cases[i]));

            if (match) {
                printf("    ✅ Roundtrip successful: %s\n", formatted->chars);
            } else {
                printf("    ❌ Mismatch!\n");
                printf("      Original: %s\n", original);
                printf("      Result:   %s\n", formatted->chars);
                all_passed = false;
            }
        }

        // Cleanup
        free(content_copy);
        free(input_type);
        free(input_flavor_str);
        free(output_type);
        free(output_flavor);
        url_destroy(test_url);
    }

    return all_passed;
}

// Test for inline math expressions
TEST_F(MathRoundtripTest, InlineMathRoundtrip) {
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
    EXPECT_TRUE(result) << "Inline math roundtrip test failed";
}

// Test roundtrip for block math expressions
TEST_F(MathRoundtripTest, BlockMathRoundtrip) {
    // Test cases: block math expressions
    const char* test_cases[] = {
        "$$E = mc^2$$",
        "$$x^2 + y^2 = z^2$$",
        "$$\\alpha + \\beta = \\gamma$$",
        "$$\\frac{1}{2}$$",
        "$$\\sqrt{x + y}$$"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "block_math", "block_math_roundtrip", "Block math"
    );
    EXPECT_TRUE(result) << "Block math roundtrip test failed";
}

// Test roundtrip for pure math expressions
TEST_F(MathRoundtripTest, PureMathRoundtrip) {
    // Test cases for pure math (without markdown delimiters)
    // Note: \begin{...}\end{...} environments are not yet supported by the tree-sitter parser
    const char* test_cases[] = {
        "E = mc^2",
        "x^2 + y^2 = z^2",
        "\\alpha + \\beta = \\gamma",
        "\\frac{1}{2}",
        "\\sqrt{x + y}",
        "\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}",
        "\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}",
        "\\lim_{x \\to 0} \\frac{\\sin x}{x} = 1"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "math", "latex",
        "pure_math", "pure_math_roundtrip", "Pure math"
    );
    EXPECT_TRUE(result) << "Pure math roundtrip test failed";
}

TEST_F(MathRoundtripTest, MinimalMarkdownTest) {
    const char* test_cases[] = {
        "# Simple Test\n\nThis is a test.\n"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "markdown", "minimal_markdown_test", "Minimal markdown"
    );
    EXPECT_TRUE(result) << "Minimal markdown test failed";
}

TEST_F(MathRoundtripTest, SmallMathTest) {
    const char* test_cases[] = {
        "$x = 1$"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "inline_math", "small_math_test", "Small math"
    );
    EXPECT_TRUE(result) << "Small math test failed";
}

TEST_F(MathRoundtripTest, SpacingTest) {
    const char* test_cases[] = {
        "$\\quad x \\quad y$"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "inline_math", "spacing_test", "Spacing command"
    );
    EXPECT_TRUE(result) << "Spacing command test failed";
}

TEST_F(MathRoundtripTest, SimpleMarkdownRoundtrip) {
    const char* test_cases[] = {
        "# Heading\n\nSome text with $x = 1$ math.\n"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "markdown_with_math", "simple_markdown_roundtrip", "Simple markdown roundtrip"
    );
    EXPECT_TRUE(result) << "Simple markdown roundtrip test failed";
}

TEST_F(MathRoundtripTest, IndexedMathTest) {
    const char* test_cases[] = {
        "$x_1 + x_2 = x_3$"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "inline_math", "indexed_math_test", "Indexed math"
    );
    EXPECT_TRUE(result) << "Indexed math test failed";
}

// Test matrix environment
TEST_F(MathRoundtripTest, MatrixTest) {
    const char* test_cases[] = {
        "\\begin{matrix} a & b \\\\ c & d \\end{matrix}"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "math", "latex",
        "pure_math", "matrix_test", "Matrix"
    );
    EXPECT_TRUE(result) << "Matrix test should pass";
}

// Test aligned environment
TEST_F(MathRoundtripTest, AlignedTest) {
    const char* test_cases[] = {
        // Using simpler expressions without parentheses (which are not yet fully supported)
        "$$\\begin{aligned} x &= a + b \\\\ y &= c + d \\end{aligned}$$"
    };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    bool result = test_math_expressions_roundtrip(
        test_cases, num_cases, "markdown", "commonmark",
        "block_math", "aligned_test", "Aligned"
    );
    EXPECT_TRUE(result) << "Aligned test should pass";
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

// Helper function to extract math expressions from indexed_math_test.md
std::vector<std::string> extract_indexed_math_expressions(const char* filepath) {
    std::vector<std::string> expressions;

    char* content = read_text_file(filepath);
    if (!content) {
        printf("ERROR: Failed to read file: %s\n", filepath);
        return expressions;
    }

    std::string text(content);
    free(content);

    // Pattern: **Expr N:** followed by math expression
    // Math can be inline ($...$) or display ($$...$$)
    std::regex expr_pattern(R"(\*\*Expr\s+\d+:\*\*\s*(\$\$?[^$]+\$\$?))");

    std::sregex_iterator it(text.begin(), text.end(), expr_pattern);
    std::sregex_iterator end;

    while (it != end) {
        std::smatch match = *it;
        if (match.size() >= 2) {
            std::string expr = match[1].str();
            // Skip moved expressions
            if (expr.find("MOVED") == std::string::npos) {
                expressions.push_back(expr);
            }
        }
        ++it;
    }

    return expressions;
}

// Test comprehensive indexed math expressions from file
TEST_F(MathRoundtripTest, IndexedMathFileTest) {
    const char* filepath = "test/input/indexed_math_test.md";

    std::vector<std::string> expressions = extract_indexed_math_expressions(filepath);

    if (expressions.empty()) {
        FAIL() << "No math expressions found in " << filepath;
    }

    printf("Testing %zu expressions from %s\n", expressions.size(), filepath);

    int passed = 0;
    int failed = 0;

    for (size_t i = 0; i < expressions.size(); i++) {
        const std::string& expr_str = expressions[i];
        const char* expr = expr_str.c_str();

        // Determine format based on delimiters
        bool is_inline = (expr[0] == '$' && expr[1] != '$');
        const char* input_format = "markdown";
        const char* input_flavor = "commonmark";

        // Create URL for the test
        char url_str[512];
        snprintf(url_str, sizeof(url_str), "test://indexed_math_expr_%zu", i + 1);
        Url* test_url = url_parse(url_str);

        // Parse input
        String* input_type = create_lambda_string(input_format);
        String* input_flavor_str = create_lambda_string(input_flavor);

        char* content_copy = strdup(expr);
        Item parsed = input_from_source(content_copy, test_url, input_type, input_flavor_str);
        Input* input = parsed.element ? (Input*)parsed.element : nullptr;

        if (!input) {
            printf("  Expr %zu: ❌ Parse failed: %s\n", i + 1, expr);
            failed++;
            free(content_copy);
            free(input_type);
            free(input_flavor_str);
            url_destroy(test_url);
            continue;
        }

        // Format back to markdown
        String* output_type = create_lambda_string(input_format);
        String* output_flavor = create_lambda_string(input_flavor);

        String* formatted = format_data(input->root, output_type, output_flavor, input->pool);

        if (!formatted || formatted->len == 0) {
            printf("  Expr %zu: ❌ Format failed: %s\n", i + 1, expr);
            failed++;
        } else {
            std::string result(formatted->chars, formatted->len);
            std::string original(expr);

            // Use semantic equivalence check (same as other tests)
            bool match = are_expressions_semantically_equivalent(result, original);

            if (match) {
                passed++;
            } else {
                printf("  Expr %zu: ❌ Mismatch\n", i + 1);
                printf("    Original: %s\n", expr);
                printf("    Result:   %s\n", result.c_str());
                failed++;
            }
        }

        free(content_copy);
        free(input_type);
        free(input_flavor_str);
        free(output_type);
        free(output_flavor);
        url_destroy(test_url);
    }

    printf("Results: %d passed, %d failed out of %zu total\n",
           passed, failed, expressions.size());

    EXPECT_EQ(failed, 0) << "Some indexed math expressions failed roundtrip";
}
