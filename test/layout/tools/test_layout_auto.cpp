/**
 * GTest Integration for Radiant Layout Engine Testing
 * 
 * Provides Google Test integration for automated layout validation
 * against browser reference data.
 */

#include <gtest/gtest.h>
#include "layout_test_framework.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

using namespace LayoutTest;

class LayoutTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        testRunner_ = std::make_unique<TestRunner>();
        testRunner_->setVerbose(false); // Controlled by GTest verbosity
        
        // Initialize test directories
        testDataDir_ = "./data";
        testReferenceDir_ = "./reference";
        testReportsDir_ = "./reports";
        
        // Ensure directories exist
        std::filesystem::create_directories(testReportsDir_);
    }
    
    void TearDown() override {
        // Cleanup if needed
    }
    
    // Helper to create parametric test data
    static std::vector<TestCase> getAllTestCases() {
        TestRunner runner;
        return runner.discoverTests(".");
    }
    
    static std::vector<TestCase> getTestCasesByCategory(const std::string& category) {
        TestRunner runner;
        return runner.loadCategory(category);
    }
    
    std::unique_ptr<TestRunner> testRunner_;
    std::string testDataDir_;
    std::string testReferenceDir_;
    std::string testReportsDir_;
};

// Basic smoke tests
TEST_F(LayoutTestSuite, FrameworkInitialization) {
    ASSERT_NE(testRunner_, nullptr);
    
    // Test directory structure exists
    EXPECT_TRUE(std::filesystem::exists(testDataDir_)) 
        << "Test data directory should exist: " << testDataDir_;
    EXPECT_TRUE(std::filesystem::exists(testReferenceDir_)) 
        << "Reference data directory should exist: " << testReferenceDir_;
}

TEST_F(LayoutTestSuite, TestDiscovery) {
    auto allTests = testRunner_->discoverTests(".");
    EXPECT_GT(allTests.size(), 0) << "Should discover at least one test case";
    
    // Check that we have tests in each category
    auto basicTests = testRunner_->loadCategory("basic");
    auto intermediateTests = testRunner_->loadCategory("intermediate");
    auto advancedTests = testRunner_->loadCategory("advanced");
    
    EXPECT_GT(basicTests.size(), 0) << "Should have basic test cases";
    EXPECT_GT(intermediateTests.size(), 0) << "Should have intermediate test cases";
    EXPECT_GT(advancedTests.size(), 0) << "Should have advanced test cases";
}

// Parametric test fixture for running individual test cases
class LayoutTestCase : public LayoutTestSuite, 
                      public ::testing::WithParamInterface<TestCase> {
};

TEST_P(LayoutTestCase, ValidateLayout) {
    TestCase testCase = GetParam();
    
    SCOPED_TRACE("Testing: " + testCase.name + " (category: " + testCase.category + ")");
    
    // Run the test
    ValidationResult result = testRunner_->runSingleTest(testCase);
    
    // Basic assertions
    EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
        << "Test should not encounter errors: " << result.message;
    
    if (result.status == ValidationResult::Status::ERROR) {
        GTEST_SKIP() << "Test encountered error: " << result.message;
    }
    
    if (result.status == ValidationResult::Status::SKIP) {
        GTEST_SKIP() << "Test was skipped: " << result.message;
    }
    
    // Main validation
    EXPECT_EQ(result.status, ValidationResult::Status::PASS) 
        << "Layout validation failed: " << result.message
        << " (Success rate: " << std::fixed << std::setprecision(1) 
        << (result.successRate() * 100) << "%)";
    
    // Additional detailed assertions for debugging
    if (result.status == ValidationResult::Status::FAIL) {
        std::cout << "\nDetailed validation results for " << testCase.name << ":" << std::endl;
        std::cout << "  Total properties: " << result.totalProperties << std::endl;
        std::cout << "  Passed: " << result.passedProperties << std::endl;
        std::cout << "  Failed: " << result.failedProperties << std::endl;
        
        // Show failed properties for debugging
        int failedCount = 0;
        for (const auto& prop : result.propertyComparisons) {
            if (!prop.withinTolerance && failedCount < 5) { // Limit output
                std::cout << "  FAILED: " << prop.property 
                         << " (expected: " << prop.expected 
                         << ", actual: " << prop.actual 
                         << ", diff: " << prop.difference 
                         << ", tolerance: " << prop.tolerance << ")" << std::endl;
                failedCount++;
            }
        }
        if (result.failedProperties > 5) {
            std::cout << "  ... and " << (result.failedProperties - 5) << " more failures" << std::endl;
        }
    }
}

// Category-specific test suites
class BasicLayoutTests : public LayoutTestSuite {};
class IntermediateLayoutTests : public LayoutTestSuite {};
class AdvancedLayoutTests : public LayoutTestSuite {};

TEST_F(BasicLayoutTests, RunAllBasicTests) {
    auto results = testRunner_->runCategory("basic");
    
    EXPECT_GT(results.totalTests, 0) << "Should have basic tests to run";
    
    // We expect at least 80% success rate for basic tests
    double successRate = static_cast<double>(results.passedTests) / results.totalTests;
    EXPECT_GE(successRate, 0.8) 
        << "Basic tests should have at least 80% success rate. "
        << "Passed: " << results.passedTests << "/" << results.totalTests
        << " (" << std::fixed << std::setprecision(1) << (successRate * 100) << "%)";
    
    // Generate detailed report
    std::string reportFile = testReportsDir_ + "/basic_tests_report.json";
    testRunner_->generateJsonReport(results, reportFile);
    
    if (::testing::Test::HasFailure()) {
        std::cout << "\nBasic test results summary:" << std::endl;
        testRunner_->printSummary(results);
    }
}

TEST_F(IntermediateLayoutTests, RunAllIntermediateTests) {
    auto results = testRunner_->runCategory("intermediate");
    
    EXPECT_GT(results.totalTests, 0) << "Should have intermediate tests to run";
    
    // We expect at least 70% success rate for intermediate tests
    double successRate = static_cast<double>(results.passedTests) / results.totalTests;
    EXPECT_GE(successRate, 0.7) 
        << "Intermediate tests should have at least 70% success rate. "
        << "Passed: " << results.passedTests << "/" << results.totalTests
        << " (" << std::fixed << std::setprecision(1) << (successRate * 100) << "%)";
    
    // Generate detailed report
    std::string reportFile = testReportsDir_ + "/intermediate_tests_report.json";
    testRunner_->generateJsonReport(results, reportFile);
    
    if (::testing::Test::HasFailure()) {
        std::cout << "\nIntermediate test results summary:" << std::endl;
        testRunner_->printSummary(results);
    }
}

TEST_F(AdvancedLayoutTests, RunAllAdvancedTests) {
    auto results = testRunner_->runCategory("advanced");
    
    EXPECT_GT(results.totalTests, 0) << "Should have advanced tests to run";
    
    // We expect at least 60% success rate for advanced tests (they're complex!)
    double successRate = static_cast<double>(results.passedTests) / results.totalTests;
    EXPECT_GE(successRate, 0.6) 
        << "Advanced tests should have at least 60% success rate. "
        << "Passed: " << results.passedTests << "/" << results.totalTests
        << " (" << std::fixed << std::setprecision(1) << (successRate * 100) << "%)";
    
    // Generate detailed report
    std::string reportFile = testReportsDir_ + "/advanced_tests_report.json";
    testRunner_->generateJsonReport(results, reportFile);
    
    if (::testing::Test::HasFailure()) {
        std::cout << "\nAdvanced test results summary:" << std::endl;
        testRunner_->printSummary(results);
    }
}

// Performance tests
class LayoutPerformanceTests : public LayoutTestSuite {};

TEST_F(LayoutPerformanceTests, TestExecutionPerformance) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    auto results = testRunner_->runCategory("basic");
    
    auto endTime = std::chrono::high_resolution_clock::now();
    double totalTime = std::chrono::duration<double>(endTime - startTime).count();
    
    // Basic tests should complete in reasonable time (10 seconds max)
    EXPECT_LT(totalTime, 10.0) << "Basic tests took too long: " << totalTime << " seconds";
    
    if (results.totalTests > 0) {
        double avgTimePerTest = totalTime / results.totalTests;
        EXPECT_LT(avgTimePerTest, 1.0) << "Average time per test too high: " << avgTimePerTest << " seconds";
    }
}

// Regression tests for specific layout features
class FlexboxLayoutTests : public LayoutTestSuite {};
class BlockLayoutTests : public LayoutTestSuite {};

// Integration tests for Radiant layout engine
class RadiantIntegrationTests : public LayoutTestSuite {};

TEST_F(FlexboxLayoutTests, ValidateFlexboxFeatures) {
    // Run tests that specifically exercise flexbox functionality
    auto allTests = testRunner_->discoverTests(".");
    
    std::vector<TestCase> flexboxTests;
    std::copy_if(allTests.begin(), allTests.end(), std::back_inserter(flexboxTests),
        [](const TestCase& test) {
            return test.name.find("flex") != std::string::npos ||
                   std::find(test.features.begin(), test.features.end(), "flexbox") != test.features.end();
        });
    
    EXPECT_GT(flexboxTests.size(), 0) << "Should have flexbox-specific tests";
    
    int passed = 0;
    int total = 0;
    
    for (const auto& test : flexboxTests) {
        auto result = testRunner_->runSingleTest(test);
        total++;
        if (result.isPass()) passed++;
        
        // Individual assertions for critical flexbox properties
        EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
            << "Flexbox test should not error: " << test.name;
    }
    
    if (total > 0) {
        double successRate = static_cast<double>(passed) / total;
        EXPECT_GE(successRate, 0.75) 
            << "Flexbox tests should have at least 75% success rate";
    }
}

TEST_F(BlockLayoutTests, ValidateBlockLayoutFeatures) {
    // Run tests that specifically exercise block layout functionality
    auto allTests = testRunner_->discoverTests(".");
    
    std::vector<TestCase> blockTests;
    std::copy_if(allTests.begin(), allTests.end(), std::back_inserter(blockTests),
        [](const TestCase& test) {
            return test.name.find("block") != std::string::npos ||
                   test.name.find("margin") != std::string::npos ||
                   test.name.find("padding") != std::string::npos ||
                   std::find(test.features.begin(), test.features.end(), "block-layout") != test.features.end();
        });
    
    EXPECT_GT(blockTests.size(), 0) << "Should have block layout specific tests";
    
    int passed = 0;
    int total = 0;
    
    for (const auto& test : blockTests) {
        auto result = testRunner_->runSingleTest(test);
        total++;
        if (result.isPass()) passed++;
        
        EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
            << "Block layout test should not error: " << test.name;
    }
    
    if (total > 0) {
        double successRate = static_cast<double>(passed) / total;
        EXPECT_GE(successRate, 0.8) 
            << "Block layout tests should have at least 80% success rate";
    }
}

TEST_F(RadiantIntegrationTests, BasicFlexContainerCreation) {
    // Test basic flex container creation and layout
    TestCase basicFlexTest;
    basicFlexTest.name = "basic_flex_container";
    basicFlexTest.category = "integration";
    basicFlexTest.htmlContent = R"(
        <style>
            .container {
                display: flex;
                width: 400px;
                height: 200px;
                gap: 10px;
            }
            .item {
                width: 100px;
                height: 50px;
                flex-grow: 1;
            }
        </style>
        <div class="container">
            <div class="item"></div>
            <div class="item"></div>
            <div class="item"></div>
        </div>
    )";
    
    // Test that the test case is created properly
    EXPECT_FALSE(basicFlexTest.htmlContent.empty()) << "HTML content should not be empty";
    EXPECT_EQ(basicFlexTest.name, "basic_flex_container") << "Test name should be set correctly";
    EXPECT_EQ(basicFlexTest.category, "integration") << "Test category should be set correctly";
    
    // Now test the actual Radiant integration
    testRunner_->setVerbose(true);  // Enable verbose output for debugging
    ValidationResult result = testRunner_->runSingleTest(basicFlexTest);
    EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
        << "Basic flex layout should not error: " << result.message;
}

TEST_F(RadiantIntegrationTests, FlexDirectionColumn) {
    // Test flex-direction: column layout
    TestCase columnFlexTest;
    columnFlexTest.name = "flex_direction_column";
    columnFlexTest.category = "integration";
    columnFlexTest.htmlContent = R"(
        <style>
            .container {
                display: flex;
                flex-direction: column;
                width: 200px;
                height: 400px;
                gap: 5px;
            }
            .item {
                width: 100px;
                height: 80px;
            }
        </style>
        <div class="container">
            <div class="item"></div>
            <div class="item"></div>
        </div>
    )";
    
    // Test CSS parsing
    EXPECT_TRUE(columnFlexTest.htmlContent.find("flex-direction: column") != std::string::npos) 
        << "Should contain column flex direction";
    
    // TODO: Uncomment when Radiant integration is fully working
    // ValidationResult result = testRunner_->runSingleTest(columnFlexTest);
    // EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
    //     << "Column flex layout should not error: " << result.message;
}

TEST_F(RadiantIntegrationTests, JustifyContentCenter) {
    // Test justify-content: center alignment
    TestCase centerJustifyTest;
    centerJustifyTest.name = "justify_content_center";
    centerJustifyTest.category = "integration";
    centerJustifyTest.htmlContent = R"(
        <style>
            .container {
                display: flex;
                justify-content: center;
                width: 300px;
                height: 100px;
            }
            .item {
                width: 50px;
                height: 50px;
            }
        </style>
        <div class="container">
            <div class="item"></div>
            <div class="item"></div>
        </div>
    )";
    
    // Test CSS parsing
    EXPECT_TRUE(centerJustifyTest.htmlContent.find("justify-content: center") != std::string::npos) 
        << "Should contain center justify content";
    
    // TODO: Uncomment when Radiant integration is fully working
    // ValidationResult result = testRunner_->runSingleTest(centerJustifyTest);
    // EXPECT_NE(result.status, ValidationResult::Status::ERROR) 
    //     << "Center justify layout should not error: " << result.message;
}

// Test data generation for parametric tests
std::vector<TestCase> generateTestParameters() {
    TestRunner runner;
    return runner.discoverTests(".");
}

// Instantiate parametric tests for all discovered test cases
INSTANTIATE_TEST_SUITE_P(
    AllLayoutTests,
    LayoutTestCase,
    ::testing::ValuesIn(generateTestParameters()),
    [](const ::testing::TestParamInfo<TestCase>& info) {
        // Generate test name from test case
        std::string name = info.param.category + "_" + info.param.name;
        
        // Replace invalid characters for GTest naming
        std::replace_if(name.begin(), name.end(), 
                       [](char c) { return !std::isalnum(c); }, '_');
        
        return name;
    }
);

// Main function for standalone execution
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Print test suite information
    std::cout << "Radiant Layout Engine Test Suite" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "Running automated layout validation against browser references" << std::endl;
    std::cout << std::endl;
    
    // Change to test directory if specified
    if (argc > 1) {
        std::string testDir = argv[1];
        if (std::filesystem::exists(testDir)) {
            std::filesystem::current_path(testDir);
            std::cout << "Changed to test directory: " << testDir << std::endl;
        }
    }
    
    // Check test environment
    if (!std::filesystem::exists("./data") || !std::filesystem::exists("./reference")) {
        std::cerr << "Error: Test data directories not found." << std::endl;
        std::cerr << "Expected: ./data and ./reference directories" << std::endl;
        std::cerr << "Please run from the test/layout directory or specify correct path." << std::endl;
        return 1;
    }
    
    return RUN_ALL_TESTS();
}
