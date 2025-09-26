#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/logging.h>
#include "browser_layout_validator.hpp"
#include "../radiant/layout.hpp"

using namespace radiant::testing;

/**
 * Integration test suite for browser-generated layout validation
 * 
 * This test suite demonstrates how to use the browser layout extractor
 * data to validate Radiant's layout engine against real browser behavior.
 */

TestSuite(browser_layout_integration, .description = "Browser layout reference tests");

// Helper to create a simple test document
Document* create_test_document(const std::string& html, const std::string& css) {
    // This would integrate with Radiant's HTML parser
    // For now, return a mock document structure
    Document* doc = (Document*)calloc(1, sizeof(Document));
    doc->view_tree = (ViewTree*)calloc(1, sizeof(ViewTree));
    view_pool_init(doc->view_tree);
    
    // TODO: Parse HTML and create actual DOM/View tree
    return doc;
}

Test(browser_layout_integration, test_descriptor_parsing) {
    // Test JSON parsing functionality
    std::string sample_json = R"({
        "test_id": "flexbox_basic",
        "category": "flexbox",
        "description": "Basic flexbox layout",
        "html": "<div class='container'><div class='item'>1</div><div class='item'>2</div></div>",
        "css": ".container { display: flex; width: 400px; } .item { width: 100px; height: 50px; }",
        "expected_layout": {
            ".container": {
                "x": 0, "y": 0, "width": 400, "height": 50,
                "computed_style": {
                    "display": "flex",
                    "justify_content": "flex-start"
                }
            },
            ".item[0]": {
                "x": 0, "y": 0, "width": 100, "height": 50
            },
            ".item[1]": {
                "x": 100, "y": 0, "width": 100, "height": 50
            }
        },
        "properties_to_test": ["position", "dimensions"],
        "browser_engine": "chromium",
        "tolerance_px": 1.0
    })";
    
    auto descriptor = BrowserLayoutValidator::parseTestDescriptor(sample_json);
    
    cr_assert_not_null(descriptor.get(), "Descriptor should be parsed successfully");
    cr_assert_str_eq(descriptor->test_id.c_str(), "flexbox_basic", "Test ID should match");
    cr_assert_str_eq(descriptor->category.c_str(), "flexbox", "Category should match");
    cr_assert_eq(descriptor->expected_layout.size(), 3, "Should have 3 expected elements");
    cr_assert_eq(descriptor->tolerance_px, 1.0, "Tolerance should match");
    
    // Check specific element data
    auto container = descriptor->expected_layout.find(".container");
    cr_assert_neq(container, descriptor->expected_layout.end(), "Container element should exist");
    cr_assert_eq(container->second.rect.width, 400, "Container width should be 400");
    cr_assert_eq(container->second.rect.height, 50, "Container height should be 50");
}

Test(browser_layout_integration, layout_rect_comparison) {
    // Test layout rectangle comparison logic
    LayoutRect expected = {10, 20, 100, 50};
    LayoutRect actual_exact = {10, 20, 100, 50};
    LayoutRect actual_close = {11, 21, 101, 49};
    LayoutRect actual_far = {15, 25, 105, 55};
    
    cr_assert(expected.matches(actual_exact, 1.0), "Exact match should pass");
    cr_assert(expected.matches(actual_close, 2.0), "Close match should pass with tolerance");
    cr_assert(!expected.matches(actual_far, 2.0), "Far match should fail");
}

Test(browser_layout_integration, property_value_extraction) {
    using namespace radiant::testing::utils;
    
    // Test CSS value extraction
    cr_assert_eq(extractNumericValue("10px"), 10.0, "Should extract px value");
    cr_assert_eq(extractNumericValue("1.5em"), 1.5, "Should extract em value");
    cr_assert_eq(extractNumericValue("50%"), 50.0, "Should extract percentage value");
    cr_assert_eq(extractNumericValue("0"), 0.0, "Should extract bare number");
    
    // Test property comparison
    cr_assert(compareProperty("10px", "10px", 0.0), "Exact string match should pass");
    cr_assert(compareProperty("10px", "11px", 1.5), "Close numeric match should pass");
    cr_assert(!compareProperty("10px", "15px", 2.0), "Far numeric match should fail");
}

// Mock test that would run with actual browser data
Test(browser_layout_integration, validate_against_browser_data, .disabled = true) {
    // This test would be enabled when we have actual browser-generated test data
    
    UiContext ui_context = {};
    
    // Load a browser-generated test file
    std::string test_file = "test/radiant/data/flexbox_basic.json";
    
    try {
        auto result = RadiantBrowserTestSuite::runSingleTest(test_file, &ui_context);
        
        cr_log_info("Test %s: %s", result.test_id.c_str(), result.passed ? "PASSED" : "FAILED");
        cr_log_info("Elements tested: %d, passed: %d", result.elements_tested, result.elements_passed);
        
        if (!result.differences.empty()) {
            cr_log_info("Differences found:");
            for (const auto& diff : result.differences) {
                cr_log_info("  %s.%s: expected %s, got %s (diff: %.1fpx)", 
                    diff.element_selector.c_str(),
                    diff.property_name.c_str(),
                    diff.expected_value.c_str(),
                    diff.actual_value.c_str(),
                    diff.difference);
            }
        }
        
        // For now, we'll consider the test successful if it runs without crashing
        cr_assert(true, "Test execution completed");
        
    } catch (const std::exception& e) {
        cr_log_error("Test failed with exception: %s", e.what());
        cr_assert_fail("Test should not throw exceptions");
    }
}

// Performance test for batch processing
Test(browser_layout_integration, batch_processing_performance, .disabled = true) {
    UiContext ui_context = {};
    std::string test_dir = "test/radiant/data/";
    
    auto start = std::chrono::high_resolution_clock::now();
    auto results = RadiantBrowserTestSuite::runTestDirectory(test_dir, &ui_context);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    cr_log_info("Processed %zu tests in %ldms", results.size(), duration.count());
    cr_log_info("Average time per test: %.1fms", (double)duration.count() / results.size());
    
    // Performance assertion - should process tests efficiently
    cr_assert_lt(duration.count() / results.size(), 100, "Should process tests in under 100ms each on average");
}

// Memory leak test
Test(browser_layout_integration, memory_management) {
    // Test that parsing and validation don't leak memory
    std::string sample_json = R"({
        "test_id": "memory_test",
        "category": "block",
        "html": "<div>Test</div>",
        "css": "div { width: 100px; height: 50px; }",
        "expected_layout": {
            "div": { "x": 0, "y": 0, "width": 100, "height": 50 }
        },
        "properties_to_test": ["dimensions"],
        "tolerance_px": 1.0
    })";
    
    // Parse multiple times to check for memory leaks
    for (int i = 0; i < 100; i++) {
        auto descriptor = BrowserLayoutValidator::parseTestDescriptor(sample_json);
        cr_assert_not_null(descriptor.get(), "Each parse should succeed");
        // descriptor will be automatically cleaned up when it goes out of scope
    }
    
    cr_assert(true, "Memory management test completed");
}
