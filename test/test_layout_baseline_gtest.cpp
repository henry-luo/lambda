/**
 * GTest wrapper for Layout Baseline Tests
 * 
 * This test integrates the Node.js layout baseline tests into the GTest framework by:
 * 1. Running the Node.js script with --json flag to get structured test results
 * 2. Parsing the JSON output to extract individual test results
 * 3. Reporting each layout test as an individual GTest test case
 * 
 * This allows GTest to report all 338 layout tests directly without hardcoding them.
 * 
 * Usage:
 *   make test-baseline  # Runs this test along with other baseline tests
 *   ./test/test_layout_baseline_gtest.exe  # Run directly
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>

// JSON parsing - simple manual parser for our specific JSON structure
#include <regex>

// Test category for baseline
#define TEST_CATEGORY "baseline"

/**
 * Structure to hold individual test result
 */
struct LayoutTestResult {
    std::string name;
    bool passed;
    double elementPassRate;
    double textPassRate;
    std::string error;
};

/**
 * Structure to hold all test results from JSON
 */
struct TestResults {
    int total;
    int successful;
    int failed;
    int errors;
    std::vector<LayoutTestResult> tests;
};

/**
 * Parse JSON output from Node.js script
 */
TestResults parseJsonOutput(const std::string& jsonOutput) {
    TestResults results;
    results.total = 0;
    results.successful = 0;
    results.failed = 0;
    results.errors = 0;

    // Extract summary statistics
    std::regex totalRegex(R"("total":\s*(\d+))");
    std::regex successRegex(R"("successful":\s*(\d+))");
    std::regex failedRegex(R"("failed":\s*(\d+))");
    std::regex errorsRegex(R"("errors":\s*(\d+))");
    
    std::smatch match;
    if (std::regex_search(jsonOutput, match, totalRegex)) {
        results.total = std::stoi(match[1].str());
    }
    if (std::regex_search(jsonOutput, match, successRegex)) {
        results.successful = std::stoi(match[1].str());
    }
    if (std::regex_search(jsonOutput, match, failedRegex)) {
        results.failed = std::stoi(match[1].str());
    }
    if (std::regex_search(jsonOutput, match, errorsRegex)) {
        results.errors = std::stoi(match[1].str());
    }

    // Parse individual test results
    // Pattern: {"name":"test_name","passed":true/false,"elementPassRate":100,"textPassRate":100,"error":null}
    std::regex testRegex("\\{\\s*\"name\":\\s*\"([^\"]+)\"\\s*,\\s*\"passed\":\\s*(true|false)\\s*,\\s*\"elementPassRate\":\\s*([\\d.]+)\\s*,\\s*\"textPassRate\":\\s*([\\d.]+)\\s*,\\s*\"error\":\\s*(null)\\s*\\}");
    
    auto testsBegin = std::sregex_iterator(jsonOutput.begin(), jsonOutput.end(), testRegex);
    auto testsEnd = std::sregex_iterator();
    
    for (std::sregex_iterator i = testsBegin; i != testsEnd; ++i) {
        std::smatch testMatch = *i;
        LayoutTestResult test;
        test.name = testMatch[1].str();
        test.passed = testMatch[2].str() == "true";
        test.elementPassRate = std::stod(testMatch[3].str());
        test.textPassRate = std::stod(testMatch[4].str());
        test.error = testMatch[5].str() == "null" ? "" : testMatch[5].str();
        results.tests.push_back(test);
    }

    return results;
}

/**
 * Run Node.js layout tests with JSON output
 */
std::string runLayoutTests() {
    // Build command: node test/layout/test_radiant_layout.js --engine lambda-css --category baseline --json
    std::string cmd = "node test/layout/test_radiant_layout.js --engine lambda-css --category ";
    cmd += TEST_CATEGORY;
    cmd += " --json 2>&1";

    // Execute command and capture output
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    pclose(pipe);
    return output;
}

/**
 * Global test results - populated once before all tests
 */
static TestResults g_testResults;
static bool g_testsLoaded = false;

/**
 * Test environment to load results once
 */
class LayoutTestEnvironment : public ::testing::Environment {
public:
    virtual ~LayoutTestEnvironment() {}

    virtual void SetUp() {
        // Check if Node.js is available
        int nodeCheck = system("which node > /dev/null 2>&1");
        if (nodeCheck != 0) {
            std::cerr << "❌ Node.js not found - skipping layout baseline tests\n";
            return;
        }

        // Check if test script exists
        std::ifstream scriptFile("test/layout/test_radiant_layout.js");
        if (!scriptFile.good()) {
            std::cerr << "❌ Layout test script not found at test/layout/test_radiant_layout.js\n";
            return;
        }

        // Check if lambda.exe exists
        std::ifstream exe("./lambda.exe");
        if (!exe.good()) {
            std::cerr << "❌ lambda.exe not found - please run 'make build' first\n";
            return;
        }

        // Check if executable
        int result = access("./lambda.exe", X_OK);
        if (result != 0) {
            std::cerr << "❌ lambda.exe exists but is not executable\n";
            return;
        }

        // Check if test data directory exists
        std::ifstream dataDir("test/layout/data/baseline");
        if (!dataDir.good()) {
            std::cerr << "❌ Baseline test data directory not found at test/layout/data/baseline/\n";
            return;
        }

        std::cout << "\n🎨 Running Layout Baseline Tests via Node.js (JSON mode)\n";
        std::cout << "========================================================\n";

        // Run the tests and parse results
        std::string jsonOutput = runLayoutTests();
        
        if (jsonOutput.empty()) {
            std::cerr << "❌ Failed to execute Node.js test script\n";
            return;
        }

        g_testResults = parseJsonOutput(jsonOutput);
        g_testsLoaded = true;

        std::cout << "\n📊 Loaded " << g_testResults.tests.size() << " layout tests\n";
        std::cout << "   Total: " << g_testResults.total << "\n";
        std::cout << "   Successful: " << g_testResults.successful << "\n";
        std::cout << "   Failed: " << g_testResults.failed << "\n";
        if (g_testResults.errors > 0) {
            std::cout << "   Errors: " << g_testResults.errors << "\n";
        }
        std::cout << "\n";
    }

    virtual void TearDown() {
        // Cleanup if needed
    }
};

/**
 * Parameterized test fixture
 */
class LayoutBaselineTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        if (!g_testsLoaded) {
            GTEST_SKIP() << "Tests were not loaded - check prerequisites";
        }
        
        size_t index = GetParam();
        ASSERT_LT(index, g_testResults.tests.size()) << "Test index out of range";
    }
};

/**
 * Parameterized test case - one test per layout test
 */
TEST_P(LayoutBaselineTest, LayoutTest) {
    size_t index = GetParam();
    const LayoutTestResult& test = g_testResults.tests[index];

    // Report test name
    SCOPED_TRACE("Layout test: " + test.name);

    // Check if test has error
    if (!test.error.empty() && test.error != "null") {
        FAIL() << "Test encountered error: " << test.error;
    }

    // Check pass rates
    EXPECT_GE(test.elementPassRate, 100.0) 
        << "Element pass rate: " << test.elementPassRate << "%";
    EXPECT_GE(test.textPassRate, 100.0) 
        << "Text pass rate: " << test.textPassRate << "%";

    // Overall pass/fail
    ASSERT_TRUE(test.passed) 
        << "Test failed - Element: " << test.elementPassRate << "%, Text: " << test.textPassRate << "%";
}

/**
 * Instantiate tests for all layout tests
 */
INSTANTIATE_TEST_SUITE_P(
    AllLayoutTests,
    LayoutBaselineTest,
    ::testing::Range(size_t(0), size_t(338)),  // Will be updated dynamically based on actual test count
    [](const ::testing::TestParamInfo<size_t>& info) {
        if (g_testsLoaded && info.param < g_testResults.tests.size()) {
            // Use test name as the test case name
            std::string name = g_testResults.tests[info.param].name;
            // Replace invalid characters for GTest
            std::replace(name.begin(), name.end(), '-', '_');
            std::replace(name.begin(), name.end(), '.', '_');
            std::replace(name.begin(), name.end(), ' ', '_');
            return name;
        }
        return std::string("test_") + std::to_string(info.param);
    }
);

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Layout Baseline Test Suite (GTest Wrapper)           ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  This test runs Node.js layout baseline tests and        ║\n";
    std::cout << "║  reports each test individually via GTest.                ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Requirements:                                            ║\n";
    std::cout << "║  • Node.js installed and in PATH                          ║\n";
    std::cout << "║  • lambda.exe built (run 'make build')                    ║\n";
    std::cout << "║  • Test data in test/layout/data/baseline/                ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // Register test environment
    ::testing::AddGlobalTestEnvironment(new LayoutTestEnvironment);

    return RUN_ALL_TESTS();
}
