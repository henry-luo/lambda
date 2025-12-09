/**
 * GTest wrapper for Layout Baseline Tests
 * 
 * This test integrates the Node.js layout baseline tests into the GTest framework,
 * allowing them to be run as part of `make test-baseline`.
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
#include <regex>

// Test category for baseline
#define TEST_CATEGORY "baseline"

/**
 * Execute Node.js layout test script and capture output
 */
class LayoutTestRunner {
public:
    struct TestResult {
        bool success;
        int totalTests;
        int passedTests;
        int failedTests;
        int errorTests;
        std::string output;
        std::string errorOutput;
    };

    static TestResult runLayoutTests(const char* suite = TEST_CATEGORY) {
        TestResult result = {false, 0, 0, 0, 0, "", ""};

        // Build command: node test/layout/test_radiant_layout.js --engine lambda-css --category baseline
        std::string cmd = "node test/layout/test_radiant_layout.js --engine lambda-css --category ";
        cmd += suite;
        cmd += " 2>&1";  // Capture both stdout and stderr

        // Execute command and capture output
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            result.errorOutput = "Failed to execute layout test command";
            return result;
        }

        // Read output
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result.output += buffer;
        }

        int status = pclose(pipe);
        int exitCode = WEXITSTATUS(status);

        // Parse the output to extract test statistics
        parseTestOutput(result);

        // Consider test successful if:
        // 1. Exit code is 0
        // 2. OR we have parsed results and passed tests >= total tests - error tests
        // (allowing for expected failures but no errors)
        if (exitCode == 0 || (result.totalTests > 0 && result.errorTests == 0)) {
            result.success = true;
        } else {
            result.success = false;
            if (result.totalTests == 0) {
                result.errorOutput = "No test results found in output";
            }
        }

        return result;
    }

private:
    static void parseTestOutput(TestResult& result) {
        std::string& output = result.output;

        // Look for summary patterns like:
        // "Total Tests: 45"
        // "✅ Successful: 43"
        // "❌ Failed: 2"
        // "💥 Errors: 0"

        std::regex totalRegex(R"(Total Tests:\s*(\d+))");
        std::regex successRegex(R"(✅ Successful:\s*(\d+))");
        std::regex failedRegex(R"(❌ Failed:\s*(\d+))");
        std::regex errorRegex(R"(💥 Errors:\s*(\d+))");

        std::smatch match;

        // Extract total tests
        if (std::regex_search(output, match, totalRegex) && match.size() > 1) {
            result.totalTests = std::stoi(match[1].str());
        }

        // Extract successful tests
        if (std::regex_search(output, match, successRegex) && match.size() > 1) {
            result.passedTests = std::stoi(match[1].str());
        }

        // Extract failed tests
        if (std::regex_search(output, match, failedRegex) && match.size() > 1) {
            result.failedTests = std::stoi(match[1].str());
        }

        // Extract error count
        if (std::regex_search(output, match, errorRegex) && match.size() > 1) {
            result.errorTests = std::stoi(match[1].str());
        }

        // Validation: if we didn't find stats, try alternate parsing
        if (result.totalTests == 0) {
            // Count individual test results (PASS/FAIL lines)
            std::regex passFailRegex(R"((✅ PASS|❌ FAIL))");
            auto begin = std::sregex_iterator(output.begin(), output.end(), passFailRegex);
            auto end = std::sregex_iterator();
            result.totalTests = std::distance(begin, end);

            // Count passes
            std::regex passOnlyRegex(R"(✅ PASS)");
            begin = std::sregex_iterator(output.begin(), output.end(), passOnlyRegex);
            result.passedTests = std::distance(begin, end);

            result.failedTests = result.totalTests - result.passedTests;
        }
    }
};

/**
 * Main test case for layout baseline tests
 */
TEST(LayoutBaselineTest, RunAllBaselineTests) {
    // Check if Node.js is available
    int nodeCheck = system("which node > /dev/null 2>&1");
    if (nodeCheck != 0) {
        GTEST_SKIP() << "Node.js not found - skipping layout baseline tests";
        return;
    }

    // Check if test script exists
    std::ifstream scriptFile("test/layout/test_radiant_layout.js");
    if (!scriptFile.good()) {
        GTEST_SKIP() << "Layout test script not found at test/layout/test_radiant_layout.js";
        return;
    }

    // Run the layout tests
    std::cout << "\n🎨 Running Layout Baseline Tests via Node.js\n";
    std::cout << "=============================================\n";

    auto result = LayoutTestRunner::runLayoutTests(TEST_CATEGORY);

    // Print captured output
    std::cout << result.output;

    // If there were errors in parsing or execution, print them
    if (!result.errorOutput.empty()) {
        std::cerr << "\n❌ Test Execution Error:\n" << result.errorOutput << "\n";
    }

    // Print summary for GTest
    std::cout << "\n📊 GTest Summary:\n";
    std::cout << "   Total Tests: " << result.totalTests << "\n";
    std::cout << "   Passed: " << result.passedTests << "\n";
    std::cout << "   Failed: " << result.failedTests << "\n";
    std::cout << "   Errors: " << result.errorTests << "\n";

    // Assert expectations
    ASSERT_GT(result.totalTests, 0) << "No layout tests were found or executed";
    ASSERT_EQ(result.errorTests, 0) << "Layout tests encountered " << result.errorTests << " errors";
    
    // For baseline tests, we expect 100% pass rate (all tests should pass)
    EXPECT_EQ(result.passedTests, result.totalTests) 
        << "Baseline tests must have 100% pass rate. "
        << "Failed: " << result.failedTests << " out of " << result.totalTests;

    // Overall success check
    ASSERT_TRUE(result.success) 
        << "Layout baseline test suite failed. "
        << "Check the output above for details.";
}

/**
 * Test to verify lambda.exe exists and is executable
 */
TEST(LayoutBaselineTest, VerifyLambdaExecutable) {
    std::ifstream exe("./lambda.exe");
    ASSERT_TRUE(exe.good()) << "lambda.exe not found - please run 'make build' first";

    // Check if it's executable
    int result = access("./lambda.exe", X_OK);
    ASSERT_EQ(result, 0) << "lambda.exe exists but is not executable";
}

/**
 * Test to verify baseline test data directory exists
 */
TEST(LayoutBaselineTest, VerifyTestDataExists) {
    std::ifstream dataDir("test/layout/data/baseline");
    ASSERT_TRUE(dataDir.good()) << "Baseline test data directory not found at test/layout/data/baseline/";
}

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Layout Baseline Test Suite (GTest Wrapper)           ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  This test runs the Node.js layout baseline tests and    ║\n";
    std::cout << "║  integrates them into the GTest framework.                ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Requirements:                                            ║\n";
    std::cout << "║  • Node.js installed and in PATH                          ║\n";
    std::cout << "║  • lambda.exe built (run 'make build')                    ║\n";
    std::cout << "║  • Test data in test/layout/data/baseline/                ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    return RUN_ALL_TESTS();
}
