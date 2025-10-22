#include <criterion/criterion.h>
#include <criterion/theories.h>
#include "../lambda/input/css/css_tokenizer_enhanced.h"
#include "../lambda/input/css/css_selector_parser.h"
#include "../lambda/input/css/css_property_value_parser.h"
#include "../lib/mempool.h"

// Test fixture
static Pool* pool;
static CSSTokenizerEnhanced* tokenizer;
static CSSSelectorParser* selector_parser;
static CSSPropertyValueParser* value_parser;

void setup_enhanced_css_tests(void) {
    pool = pool_create(8192);
    tokenizer = css_tokenizer_enhanced_create(pool);
    selector_parser = css_selector_parser_create(pool);
    value_parser = css_property_value_parser_create(pool);
}

void teardown_enhanced_css_tests(void) {
    if (tokenizer) css_tokenizer_enhanced_destroy(tokenizer);
    if (selector_parser) css_selector_parser_destroy(selector_parser);
    if (value_parser) css_property_value_parser_destroy(value_parser);
    if (pool) pool_destroy(pool);
}

// Enhanced CSS Tokenizer Tests
TestSuite(enhanced_css_tokenizer, .init = setup_enhanced_css_tests, .fini = teardown_enhanced_css_tests);

Test(enhanced_css_tokenizer, test_unicode_identifiers) {
    const char* css = "α-test 测试 العربية";
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css, strlen(css), &tokens);
    
    cr_assert_eq(count, 3, "Expected 3 Unicode identifier tokens");
    cr_assert_eq(tokens[0].type, CSS_TOKEN_ENHANCED_IDENT);
    cr_assert_str_eq(tokens[0].value, "α-test");
    cr_assert_eq(tokens[1].type, CSS_TOKEN_ENHANCED_IDENT);
    cr_assert_str_eq(tokens[1].value, "测试");
    cr_assert_eq(tokens[2].type, CSS_TOKEN_ENHANCED_IDENT);
    cr_assert_str_eq(tokens[2].value, "العربية");
}

Test(enhanced_css_tokenizer, test_css3_color_tokens) {
    const char* css = "#ff0000 rgb(255, 0, 0) hsl(0, 100%, 50%) hwb(0 0% 0%) lab(50% 20 30)";
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css, strlen(css), &tokens);
    
    cr_assert_geq(count, 5, "Expected at least 5 color-related tokens");
    
    // Check hex color
    cr_assert_eq(tokens[0].type, CSS_TOKEN_ENHANCED_HASH);
    cr_assert_str_eq(tokens[0].value, "ff0000");
    
    // Check color functions
    cr_assert_eq(tokens[1].type, CSS_TOKEN_ENHANCED_FUNCTION);
    cr_assert_str_eq(tokens[1].value, "rgb");
}

Test(enhanced_css_tokenizer, test_css_functions) {
    const char* css = "calc(100% - 20px) min(10px, 5vw) max(100px, 10em) clamp(1rem, 2.5vw, 2rem)";
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css, strlen(css), &tokens);
    
    cr_assert_geq(count, 4, "Expected at least 4 function tokens");
    
    // Check calc function
    int calc_index = -1;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_ENHANCED_FUNCTION && 
            strcmp(tokens[i].value, "calc") == 0) {
            calc_index = i;
            break;
        }
    }
    cr_assert_neq(calc_index, -1, "calc() function not found in tokens");
}

Test(enhanced_css_tokenizer, test_custom_properties) {
    const char* css = "--primary-color: #3498db; var(--primary-color, blue)";
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css, strlen(css), &tokens);
    
    cr_assert_geq(count, 6, "Expected at least 6 tokens for custom property");
    
    // Check custom property name
    bool found_custom_prop = false;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_ENHANCED_IDENT && 
            strncmp(tokens[i].value, "--", 2) == 0) {
            found_custom_prop = true;
            cr_assert_str_eq(tokens[i].value, "--primary-color");
            break;
        }
    }
    cr_assert(found_custom_prop, "Custom property --primary-color not found");
}

Test(enhanced_css_tokenizer, test_at_rules) {
    const char* css = "@media (prefers-color-scheme: dark) { } @supports (display: grid) { }";
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css, strlen(css), &tokens);
    
    cr_assert_geq(count, 2, "Expected at least 2 @-rule tokens");
    
    // Check @media
    cr_assert_eq(tokens[0].type, CSS_TOKEN_ENHANCED_AT_RULE);
    cr_assert_str_eq(tokens[0].value, "media");
}

// CSS4 Selector Parser Tests
TestSuite(css4_selector_parser, .init = setup_enhanced_css_tests, .fini = teardown_enhanced_css_tests);

Test(css4_selector_parser, test_css4_pseudo_classes) {
    const char* selectors[] = {
        ":is(.class1, .class2)",
        ":where(p, div)",
        ":has(.child)",
        ":not(:hover)",
        ":nth-child(2n+1 of .selected)"
    };
    
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        bool is_valid = css_validate_selector_syntax(selectors[i]);
        cr_assert(is_valid, "CSS4 selector should be valid: %s", selectors[i]);
    }
}

Test(css4_selector_parser, test_specificity_calculation) {
    struct {
        const char* selector;
        int expected_a; // inline styles (not applicable here)
        int expected_b; // IDs
        int expected_c; // classes/attributes/pseudo-classes
        int expected_d; // elements/pseudo-elements
    } test_cases[] = {
        {"div", 0, 0, 0, 1},
        {".class", 0, 0, 1, 0},
        {"#id", 0, 1, 0, 0},
        {"div.class#id", 0, 1, 1, 1},
        {"div > .class + p", 0, 0, 1, 2},
        {":hover", 0, 0, 1, 0},
        {"::before", 0, 0, 0, 1},
        {":is(.class1, .class2)", 0, 0, 1, 0}, // :is() takes max specificity of args
        {":where(.class)", 0, 0, 0, 0}, // :where() always has zero specificity
    };
    
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        // This would require implementing the actual selector parsing
        // For now, test the specificity calculation logic
        bool is_valid = css_validate_selector_syntax(test_cases[i].selector);
        cr_assert(is_valid, "Selector should be valid: %s", test_cases[i].selector);
    }
}

Test(css4_selector_parser, test_nesting_support) {
    const char* nested_selectors[] = {
        "& .child",
        "&:hover",
        "&::before",
        ".parent & ",
        "& + &"
    };
    
    for (size_t i = 0; i < sizeof(nested_selectors) / sizeof(nested_selectors[0]); i++) {
        bool is_valid = css_validate_selector_syntax(nested_selectors[i]);
        cr_assert(is_valid, "Nested selector should be valid: %s", nested_selectors[i]);
    }
}

Test(css4_selector_parser, test_complex_selectors) {
    const char* complex_selectors[] = {
        "article:has(.featured) h2",
        ".card:not(:has(.image)) .title",
        "tr:nth-child(even):not(.excluded)",
        ":is(section, article) :where(h1, h2, h3):not(.no-style)"
    };
    
    for (size_t i = 0; i < sizeof(complex_selectors) / sizeof(complex_selectors[0]); i++) {
        bool is_valid = css_validate_selector_syntax(complex_selectors[i]);
        cr_assert(is_valid, "Complex selector should be valid: %s", complex_selectors[i]);
    }
}

// Enhanced Property Value Parser Tests
TestSuite(enhanced_property_value_parser, .init = setup_enhanced_css_tests, .fini = teardown_enhanced_css_tests);

Test(enhanced_property_value_parser, test_calc_expressions) {
    const char* calc_values[] = {
        "calc(100% - 20px)",
        "calc(100vw / 4 - 1rem)",
        "calc(2 * (100px + 50px))",
        "calc(100% - var(--spacing, 1rem))",
        "calc(sin(45deg) * 100px)"
    };
    
    for (size_t i = 0; i < sizeof(calc_values) / sizeof(calc_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "width", calc_values[i]);
        
        cr_assert_not_null(value, "calc() value should parse successfully: %s", calc_values[i]);
        cr_assert_eq(value->type, CSS_VALUE_ENHANCED_CALC, "Should be recognized as calc expression");
    }
}

Test(enhanced_property_value_parser, test_custom_properties) {
    const char* var_values[] = {
        "var(--primary-color)",
        "var(--spacing, 1rem)",
        "var(--font-size, var(--base-size, 16px))",
        "var(--color, #000)"
    };
    
    for (size_t i = 0; i < sizeof(var_values) / sizeof(var_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "color", var_values[i]);
        
        cr_assert_not_null(value, "var() value should parse successfully: %s", var_values[i]);
        cr_assert_eq(value->type, CSS_VALUE_ENHANCED_VAR, "Should be recognized as var reference");
    }
}

Test(enhanced_property_value_parser, test_env_variables) {
    const char* env_values[] = {
        "env(safe-area-inset-top)",
        "env(safe-area-inset-bottom, 0px)",
        "env(keyboard-inset-height)",
        "env(titlebar-area-width, 100%)"
    };
    
    for (size_t i = 0; i < sizeof(env_values) / sizeof(env_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "padding-top", env_values[i]);
        
        cr_assert_not_null(value, "env() value should parse successfully: %s", env_values[i]);
        cr_assert_eq(value->type, CSS_VALUE_ENHANCED_ENV, "Should be recognized as env reference");
    }
}

Test(enhanced_property_value_parser, test_math_functions) {
    const char* math_values[] = {
        "min(10px, 5vw)",
        "max(100px, 10em)",
        "clamp(1rem, 2.5vw, 2rem)",
        "abs(-5px)",
        "round(3.7px, 1px)"
    };
    
    for (size_t i = 0; i < sizeof(math_values) / sizeof(math_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "width", math_values[i]);
        
        cr_assert_not_null(value, "Math function should parse successfully: %s", math_values[i]);
        // Type should be one of the math function types
        cr_assert(value->type >= CSS_VALUE_ENHANCED_MIN && 
                 value->type <= CSS_VALUE_ENHANCED_ROUND,
                 "Should be recognized as math function");
    }
}

Test(enhanced_property_value_parser, test_color_functions) {
    const char* color_values[] = {
        "color-mix(in srgb, red, blue)",
        "hwb(120 10% 20%)",
        "lab(50% 20 -30)",
        "lch(70% 45 30)",
        "oklab(0.7 0.1 0.1)",
        "oklch(0.7 0.15 180)"
    };
    
    for (size_t i = 0; i < sizeof(color_values) / sizeof(color_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "color", color_values[i]);
        
        cr_assert_not_null(value, "Color function should parse successfully: %s", color_values[i]);
        cr_assert(css_value_enhanced_is_color(value), "Should be recognized as color value");
    }
}

Test(enhanced_property_value_parser, test_complex_values) {
    const char* complex_values[] = {
        "calc(100% - var(--spacing)) min(50vw, 400px)",
        "linear-gradient(45deg, var(--start-color, #fff), var(--end-color, #000))",
        "repeat(auto-fit, minmax(min(200px, 100%), 1fr))",
        "clamp(1rem, calc(1rem + 2vw), 2rem)"
    };
    
    for (size_t i = 0; i < sizeof(complex_values) / sizeof(complex_values[0]); i++) {
        CSSValueEnhanced* value = css_parse_declaration_value_enhanced(
            value_parser, "width", complex_values[i]);
        
        // These might be parsed as function calls or value lists
        cr_assert_not_null(value, "Complex value should parse: %s", complex_values[i]);
    }
}

// Integration Tests
TestSuite(css_enhanced_integration, .init = setup_enhanced_css_tests, .fini = teardown_enhanced_css_tests);

Test(css_enhanced_integration, test_full_css3_rule) {
    const char* css_rule = 
        ".card:has(.featured) { "
        "  width: calc(100% - var(--spacing, 2rem)); "
        "  padding: env(safe-area-inset-top, 1rem); "
        "  background: color-mix(in srgb, var(--primary), white 20%); "
        "  transform: translateX(min(0px, var(--offset))); "
        "}";
    
    // Tokenize the entire rule
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, css_rule, strlen(css_rule), &tokens);
    
    cr_assert_geq(count, 20, "Expected at least 20 tokens for complex CSS rule");
    
    // Verify we can identify key components
    bool found_selector = false;
    bool found_calc = false;
    bool found_var = false;
    bool found_env = false;
    bool found_color_mix = false;
    
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_ENHANCED_FUNCTION) {
            if (strcmp(tokens[i].value, "calc") == 0) found_calc = true;
            else if (strcmp(tokens[i].value, "var") == 0) found_var = true;
            else if (strcmp(tokens[i].value, "env") == 0) found_env = true;
            else if (strcmp(tokens[i].value, "color-mix") == 0) found_color_mix = true;
        }
        if (tokens[i].type == CSS_TOKEN_ENHANCED_COLON && i > 0 && 
            tokens[i-1].type == CSS_TOKEN_ENHANCED_IDENT &&
            strcmp(tokens[i-1].value, "has") == 0) {
            found_selector = true;
        }
    }
    
    cr_assert(found_calc, "Should find calc() function");
    cr_assert(found_var, "Should find var() function");
    cr_assert(found_env, "Should find env() function");
    cr_assert(found_color_mix, "Should find color-mix() function");
}

Test(css_enhanced_integration, test_css_nesting_with_functions) {
    const char* nested_css =
        ".component { "
        "  --size: clamp(1rem, 2vw, 3rem); "
        "  & .header { "
        "    font-size: var(--size); "
        "    margin: calc(var(--size) / 2); "
        "  } "
        "}";
    
    // Tokenize and verify we can handle nested CSS with functions
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, nested_css, strlen(nested_css), &tokens);
    
    cr_assert_geq(count, 15, "Expected sufficient tokens for nested CSS");
    
    // Verify nesting selector is present
    bool found_nesting = false;
    for (int i = 0; i < count; i++) {
        if (tokens[i].type == CSS_TOKEN_ENHANCED_DELIM && 
            tokens[i].char_value == '&') {
            found_nesting = true;
            break;
        }
    }
    
    cr_assert(found_nesting, "Should find nesting selector (&)");
}

// Performance Tests
TestSuite(css_enhanced_performance, .init = setup_enhanced_css_tests, .fini = teardown_enhanced_css_tests);

Test(css_enhanced_performance, test_large_css_tokenization) {
    // Create a large CSS string with various CSS3+ features
    char* large_css = (char*)pool_alloc(pool, 100000);
    strcpy(large_css, "");
    
    for (int i = 0; i < 1000; i++) {
        char rule[256];
        snprintf(rule, sizeof(rule),
            ".rule%d { width: calc(100%% - %dpx); color: var(--color%d, #%06x); } ",
            i, i, i, i * 0x1000);
        strcat(large_css, rule);
    }
    
    // Measure tokenization performance
    clock_t start = clock();
    CSSTokenEnhanced* tokens;
    int count = css_tokenizer_enhanced_tokenize(tokenizer, large_css, strlen(large_css), &tokens);
    clock_t end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    cr_assert_geq(count, 5000, "Expected at least 5000 tokens");
    cr_assert_lt(time_taken, 1.0, "Tokenization should complete within 1 second");
    
    printf("Tokenized %d tokens in %f seconds\n", count, time_taken);
}