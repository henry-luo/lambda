/**
 * CSS Test Helpers
 *
 * Shared utilities for CSS unit testing in Lambda.
 * Provides memory management, token/selector validation,
 * and common test patterns.
 */

#ifndef CSS_TEST_HELPERS_HPP
#define CSS_TEST_HELPERS_HPP

#include <gtest/gtest.h>
#include "../../../lambda/input/css/css_parser.hpp"
#include "../../../lambda/input/css/css_engine.hpp"
#include "../../../lib/mempool.h"
#include <string>
#include <vector>

namespace CssTestHelpers {

/**
 * RAII wrapper for Pool* memory management in tests
 * Automatically creates and destroys pool for each test
 */
class PoolGuard {
private:
    Pool* pool_;

public:
    PoolGuard() : pool_(pool_create()) {
        if (!pool_) {
            throw std::runtime_error("Failed to create memory pool");
        }
    }

    ~PoolGuard() {
        if (pool_) {
            pool_destroy(pool_);
        }
    }

    // Delete copy constructor and assignment
    PoolGuard(const PoolGuard&) = delete;
    PoolGuard& operator=(const PoolGuard&) = delete;

    Pool* get() { return pool_; }
    operator Pool*() { return pool_; }
};

/**
 * Token validation helpers
 */
class TokenAssertions {
public:
    // Assert token type
    static void AssertType(const CssToken* token, CssTokenType expected_type) {
        ASSERT_NE(token, nullptr) << "Token is NULL";
        EXPECT_EQ(token->type, expected_type)
            << "Expected token type " << css_token_type_to_string(expected_type)
            << " but got " << css_token_type_to_string(token->type);
    }

    // Assert token type and value
    static void AssertToken(const CssToken* token, CssTokenType expected_type,
                           const char* expected_value) {
        AssertType(token, expected_type);
        if (expected_value) {
            ASSERT_NE(token->value, nullptr) << "Token value is NULL";
            EXPECT_STREQ(token->value, expected_value)
                << "Token value mismatch";
        }
    }

    // Assert delimiter token
    static void AssertDelimiter(const CssToken* token, char expected_delimiter) {
        AssertType(token, CSS_TOKEN_DELIM);
        EXPECT_EQ(token->data.delimiter, expected_delimiter)
            << "Expected delimiter '" << expected_delimiter
            << "' but got '" << token->data.delimiter << "'";
    }

    // Assert number token
    static void AssertNumber(const CssToken* token, double expected_value) {
        ASSERT_TRUE(token->type == CSS_TOKEN_NUMBER ||
                   token->type == CSS_TOKEN_DIMENSION ||
                   token->type == CSS_TOKEN_PERCENTAGE)
            << "Expected numeric token type";
        EXPECT_DOUBLE_EQ(token->data.number_value, expected_value)
            << "Number value mismatch";
    }

    // Assert token array count
    static void AssertCount(size_t actual_count, size_t expected_count) {
        EXPECT_EQ(actual_count, expected_count)
            << "Expected " << expected_count << " tokens but got " << actual_count;
    }
};

/**
 * Selector validation helpers
 */
class SelectorAssertions {
public:
    // Assert selector type and value
    static void AssertSelector(const CssSimpleSelector* selector,
                              CssSelectorType expected_type,
                              const char* expected_value) {
        ASSERT_NE(selector, nullptr) << "Selector is NULL";
        EXPECT_EQ(selector->type, expected_type)
            << "Selector type mismatch";

        if (expected_value) {
            ASSERT_NE(selector->value, nullptr) << "Selector value is NULL";
            EXPECT_STREQ(selector->value, expected_value)
                << "Selector value mismatch";
        }
    }

    // Assert element selector
    static void AssertElement(const CssSimpleSelector* selector,
                             const char* element_name) {
        AssertSelector(selector, CSS_SELECTOR_TYPE_ELEMENT, element_name);
    }

    // Assert class selector
    static void AssertClass(const CssSimpleSelector* selector,
                           const char* class_name) {
        AssertSelector(selector, CSS_SELECTOR_TYPE_CLASS, class_name);
    }

    // Assert ID selector
    static void AssertID(const CssSimpleSelector* selector,
                        const char* id_name) {
        AssertSelector(selector, CSS_SELECTOR_TYPE_ID, id_name);
    }

    // Assert universal selector
    static void AssertUniversal(const CssSimpleSelector* selector) {
        ASSERT_NE(selector, nullptr);
        EXPECT_EQ(selector->type, CSS_SELECTOR_TYPE_UNIVERSAL);
    }
};

/**
 * Declaration validation helpers
 */
class DeclarationAssertions {
public:
    // Assert declaration property and value
    static void AssertDeclaration(const CssDeclaration* decl,
                                  CssPropertyId expected_property_id) {
        ASSERT_NE(decl, nullptr) << "Declaration is NULL";
        ASSERT_NE(decl->value, nullptr) << "Declaration value is NULL";

        EXPECT_EQ(decl->property_id, expected_property_id)
            << "Property ID mismatch";
    }

    // Assert important flag
    static void AssertImportant(const CssDeclaration* decl, bool expected_important) {
        ASSERT_NE(decl, nullptr);
        EXPECT_EQ(decl->important, expected_important)
            << "Expected important=" << expected_important;
    }

    // Assert declaration with importance
    static void AssertDeclarationWithImportance(const CssDeclaration* decl,
                                                CssPropertyId expected_property_id,
                                                bool expected_important) {
        AssertDeclaration(decl, expected_property_id);
        AssertImportant(decl, expected_important);
    }
};

/**
 * Rule validation helpers
 */
class RuleAssertions {
public:
    // Assert rule has expected number of selectors
    static void AssertSelectorCount(const CssRule* rule, size_t expected_count) {
        ASSERT_NE(rule, nullptr);
        // For style rules, check the selector in style_rule union member
        if (rule->type == CSS_RULE_STYLE) {
            EXPECT_NE(rule->data.style_rule.selector, nullptr)
                << "Expected " << expected_count << " selectors";
        }
    }

    // Assert rule has expected number of declarations
    static void AssertDeclarationCount(const CssRule* rule, size_t expected_count) {
        ASSERT_NE(rule, nullptr);
        if (rule->type == CSS_RULE_STYLE) {
            EXPECT_EQ(rule->data.style_rule.declaration_count, expected_count)
                << "Expected " << expected_count << " declarations";
        }
    }

    // Assert complete rule structure
    static void AssertRule(const CssRule* rule,
                          size_t expected_selectors,
                          size_t expected_declarations) {
        ASSERT_NE(rule, nullptr) << "Rule is NULL";
        AssertSelectorCount(rule, expected_selectors);
        AssertDeclarationCount(rule, expected_declarations);
    }
};

/**
 * Tokenization helper - wraps css_tokenize with pool management
 */
class Tokenizer {
private:
    Pool* pool_;
    CssToken* tokens_;
    size_t count_;

public:
    Tokenizer(Pool* pool, const char* css)
        : pool_(pool), tokens_(nullptr), count_(0) {
        size_t length = strlen(css);
        tokens_ = css_tokenize(css, length, pool_, &count_);
    }

    CssToken* tokens() { return tokens_; }

    // Get count excluding EOF token
    size_t count() const {
        if (count_ > 0 && tokens_[count_ - 1].type == CSS_TOKEN_EOF) {
            return count_ - 1;
        }
        return count_;
    }

    // Get total count including EOF token
    size_t total_count() const { return count_; }

    // Get token at index (with bounds checking)
    const CssToken* operator[](size_t index) const {
        if (index >= count_) return nullptr;
        return &tokens_[index];
    }
};

/**
 * CSS parsing helper - wraps common parsing operations
 */
class Parser {
private:
    Pool* pool_;

public:
    explicit Parser(Pool* pool) : pool_(pool) {}

    // Tokenize CSS
    Tokenizer Tokenize(const char* css) {
        return Tokenizer(pool_, css);
    }

    // Parse selector from CSS text
    CssSimpleSelector* ParseSelector(const char* css) {
        Tokenizer tokenizer = Tokenize(css);
        if (!tokenizer.tokens() || tokenizer.count() == 0) return nullptr;

        int pos = 0;
        return css_parse_simple_selector_from_tokens(
            tokenizer.tokens(), &pos, tokenizer.count(), pool_
        );
    }

    // Parse declaration from CSS text
    CssDeclaration* ParseDeclaration(const char* css) {
        Tokenizer tokenizer = Tokenize(css);
        if (!tokenizer.tokens() || tokenizer.count() == 0) return nullptr;

        int pos = 0;
        return css_parse_declaration_from_tokens(
            tokenizer.tokens(), &pos, tokenizer.count(), pool_
        );
    }

    // Parse rule from CSS text
    CssRule* ParseRule(const char* css) {
        Tokenizer tokenizer = Tokenize(css);
        if (!tokenizer.tokens() || tokenizer.count() == 0) return nullptr;

        return css_parse_rule_from_tokens(
            tokenizer.tokens(), tokenizer.count(), pool_
        );
    }

    // Parse stylesheet from CSS text
    CssStylesheet* ParseStylesheet(const char* css) {
        // Create a temporary engine for parsing
        CssEngine* engine = css_engine_create(pool_);
        if (!engine) return nullptr;

        CssStylesheet* stylesheet = css_parse_stylesheet(engine, css, nullptr);

        // Note: Don't destroy engine yet as stylesheet might reference it
        return stylesheet;
    }
};

/**
 * Test data structures
 */
struct TokenTestCase {
    const char* input;
    CssTokenType expected_type;
    const char* expected_value;
    size_t expected_count;

    TokenTestCase(const char* i, CssTokenType t, const char* v, size_t c)
        : input(i), expected_type(t), expected_value(v), expected_count(c) {}
};

struct SelectorTestCase {
    const char* input;
    CssSelectorType expected_type;
    const char* expected_value;

    SelectorTestCase(const char* i, CssSelectorType t, const char* v)
        : input(i), expected_type(t), expected_value(v) {}
};

struct DeclarationTestCase {
    const char* input;
    const char* expected_property;
    const char* expected_value;
    bool expected_important;

    DeclarationTestCase(const char* i, const char* p, const char* v, bool imp = false)
        : input(i), expected_property(p), expected_value(v), expected_important(imp) {}
};

/**
 * Utility functions
 */
namespace Utils {
    // Load CSS file from fixtures directory
    inline std::string LoadFixture(const char* filename) {
        std::string path = std::string("test/css/fixtures/") + filename;
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return "";

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        std::vector<char> buffer(size + 1);
        fread(buffer.data(), 1, size, f);
        buffer[size] = '\0';
        fclose(f);

        return std::string(buffer.data());
    }

    // Compare token arrays
    inline bool CompareTokenArrays(const CssToken* tokens1, size_t count1,
                                   const CssToken* tokens2, size_t count2) {
        if (count1 != count2) return false;

        for (size_t i = 0; i < count1; i++) {
            if (tokens1[i].type != tokens2[i].type) return false;
            if (tokens1[i].value && tokens2[i].value) {
                if (strcmp(tokens1[i].value, tokens2[i].value) != 0) return false;
            } else if (tokens1[i].value != tokens2[i].value) {
                return false;
            }
        }

        return true;
    }
}

} // namespace CssTestHelpers

// Convenience macros for common assertions
#define ASSERT_CSS_TOKEN(token, type, value) \
    CssTestHelpers::TokenAssertions::AssertToken(token, type, value)

#define ASSERT_CSS_TOKEN_TYPE(token, type) \
    CssTestHelpers::TokenAssertions::AssertType(token, type)

#define ASSERT_CSS_SELECTOR(selector, type, value) \
    CssTestHelpers::SelectorAssertions::AssertSelector(selector, type, value)

#define ASSERT_CSS_DECLARATION(decl, property, value) \
    CssTestHelpers::DeclarationAssertions::AssertDeclaration(decl, property, value)

#define ASSERT_CSS_RULE(rule, sel_count, decl_count) \
    CssTestHelpers::RuleAssertions::AssertRule(rule, sel_count, decl_count)

#endif // CSS_TEST_HELPERS_HPP
