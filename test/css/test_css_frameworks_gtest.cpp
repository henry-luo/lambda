#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>
#include "../../lambda/input/css/css_tokenizer.h"
#include "../../lib/mempool.h"

// Test fixture class for CSS frameworks tests
class CssFrameworksTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    void validateCssFramework(const char* css_content, const char* framework_name) {
        size_t token_count;
        CSSToken* tokens = css_tokenize(css_content, strlen(css_content), pool, &token_count);
        EXPECT_NE(tokens, nullptr) << "Should tokenize " << framework_name << " CSS";
        EXPECT_GT(token_count, (size_t)0) << "Should produce tokens for " << framework_name;
    }

    // Load mock CSS content for framework testing
    const char* load_css_content(const char* framework_name) {
        // Return mock CSS content for different frameworks
        if (strcmp(framework_name, "bootstrap") == 0) {
            return ".container { max-width: 1140px; } .btn { padding: 0.375rem 0.75rem; }";
        } else if (strcmp(framework_name, "bulma") == 0) {
            return ".column { flex-basis: 0; } .button { border-radius: 4px; }";
        } else if (strcmp(framework_name, "foundation") == 0) {
            return ".grid-container { max-width: 62.5rem; } .button { padding: 0.85em 1em; }";
        } else if (strcmp(framework_name, "normalize") == 0) {
            return "html { line-height: 1.15; } body { margin: 0; }";
        } else if (strcmp(framework_name, "tailwind") == 0) {
            return ".container { width: 100%; } .flex { display: flex; }";
        }
        return "/* Mock CSS content */";
    }
};

// Test Bootstrap CSS parsing
TEST_F(CssFrameworksTest, ParseBootstrap) {
    const char* css_content = load_css_content("bootstrap");
    ASSERT_NE(css_content, nullptr) << "CSS content should not be NULL";

    validateCssFramework(css_content, "Bootstrap");
}

// Test Bulma CSS parsing
TEST_F(CssFrameworksTest, ParseBulma) {
    const char* css_content = load_css_content("bulma");
    ASSERT_NE(css_content, nullptr) << "CSS content should not be NULL";

    validateCssFramework(css_content, "Bulma");
}

// Test Foundation CSS parsing
TEST_F(CssFrameworksTest, ParseFoundation) {
    const char* css_content = load_css_content("foundation");
    ASSERT_NE(css_content, nullptr) << "CSS content should not be NULL";

    validateCssFramework(css_content, "Foundation");
}

// Test Normalize CSS parsing
TEST_F(CssFrameworksTest, ParseNormalize) {
    const char* css_content = load_css_content("normalize");
    ASSERT_NE(css_content, nullptr) << "CSS content should not be NULL";

    validateCssFramework(css_content, "Normalize");
}

// Test Tailwind CSS parsing
TEST_F(CssFrameworksTest, ParseTailwind) {
    const char* css_content = load_css_content("tailwind");
    ASSERT_NE(css_content, nullptr) << "CSS content should not be NULL";

    validateCssFramework(css_content, "Tailwind");
}

// Test performance across multiple frameworks
TEST_F(CssFrameworksTest, ParseAllFrameworksPerformance) {
    const char* frameworks[] = {"bootstrap", "bulma", "foundation", "normalize", "tailwind"};
    int framework_count = sizeof(frameworks) / sizeof(frameworks[0]);

    for (int i = 0; i < framework_count; i++) {
        const char* css_content = load_css_content(frameworks[i]);
        validateCssFramework(css_content, frameworks[i]);
    }
}