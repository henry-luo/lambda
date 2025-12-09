/**
 * GTest for Lambda View Command
 * 
 * Tests the `lambda view` command functionality:
 * - Viewing HTML files
 * - Viewing Markdown files
 * - Auto-close functionality
 * 
 * Usage:
 *   make test-baseline  # Runs this test along with other baseline tests
 *   ./test/test_view_cmd_gtest.exe  # Run directly
 */

#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iostream>

/**
 * Test fixture for view command tests
 */
class ViewCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check if lambda.exe exists and is executable
        std::ifstream exe("./lambda.exe");
        if (!exe.good()) {
            GTEST_SKIP() << "lambda.exe not found - please run 'make build' first";
        }

        int result = access("./lambda.exe", X_OK);
        if (result != 0) {
            GTEST_SKIP() << "lambda.exe exists but is not executable";
        }
    }

    /**
     * Execute a view command with auto-close and capture output
     */
    std::pair<int, std::string> executeViewCommand(const std::string& filePath) {
        // Set environment variable for auto-close
        setenv("LAMBDA_AUTO_CLOSE", "1", 1);

        // Build command: ./lambda.exe view <file>
        std::string cmd = "./lambda.exe view " + filePath + " 2>&1";

        // Execute command and capture output
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return {-1, "Failed to execute command"};
        }

        std::string output;
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }

        int status = pclose(pipe);
        int exitCode = WEXITSTATUS(status);

        // Unset environment variable
        unsetenv("LAMBDA_AUTO_CLOSE");

        return {exitCode, output};
    }

    /**
     * Check if a file exists
     */
    bool fileExists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
    }
};

/**
 * Test viewing an HTML file
 */
TEST_F(ViewCommandTest, ViewHTMLFile) {
    // Check if test file exists
    if (!fileExists("test/html/index.html")) {
        GTEST_SKIP() << "Test file test/html/index.html not found";
    }

    std::cout << "\n📄 Testing: lambda view test/html/index.html\n";

    auto [exitCode, output] = executeViewCommand("test/html/index.html");

    // Print output for debugging
    if (!output.empty()) {
        std::cout << "Command output:\n" << output << "\n";
    }

    // Check exit code
    EXPECT_EQ(exitCode, 0) << "View command should exit successfully with code 0";

    // The command should complete without errors
    // With auto-close, the window should open and close automatically
    EXPECT_TRUE(exitCode == 0 || exitCode == 1) 
        << "Exit code should be 0 (success) or 1 (auto-closed)";
}

/**
 * Test viewing a Markdown file
 */
TEST_F(ViewCommandTest, ViewMarkdownFile) {
    // Check if test file exists
    if (!fileExists("test/input/sample.md")) {
        GTEST_SKIP() << "Test file test/input/sample.md not found";
    }

    std::cout << "\n📝 Testing: lambda view test/input/sample.md\n";

    auto [exitCode, output] = executeViewCommand("test/input/sample.md");

    // Print output for debugging
    if (!output.empty()) {
        std::cout << "Command output:\n" << output << "\n";
    }

    // Check exit code
    EXPECT_EQ(exitCode, 0) << "View command should exit successfully with code 0";

    // The command should complete without errors
    // With auto-close, the window should open and close automatically
    EXPECT_TRUE(exitCode == 0 || exitCode == 1) 
        << "Exit code should be 0 (success) or 1 (auto-closed)";
}

/**
 * Test that auto-close actually closes the window quickly
 */
TEST_F(ViewCommandTest, AutoCloseTimingHTML) {
    // Check if test file exists
    if (!fileExists("test/html/index.html")) {
        GTEST_SKIP() << "Test file test/html/index.html not found";
    }

    std::cout << "\n⏱️  Testing auto-close timing for HTML\n";

    // Measure execution time
    auto start = std::chrono::high_resolution_clock::now();
    
    auto [exitCode, output] = executeViewCommand("test/html/index.html");
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Execution time: " << duration.count() << " seconds\n";

    // With auto-close, the command should complete within a reasonable time
    // (not hang waiting for user to close window)
    EXPECT_LT(duration.count(), 10) 
        << "View command with auto-close should complete within 10 seconds";
}

/**
 * Test that auto-close actually closes the window quickly for Markdown
 */
TEST_F(ViewCommandTest, AutoCloseTimingMarkdown) {
    // Check if test file exists
    if (!fileExists("test/input/sample.md")) {
        GTEST_SKIP() << "Test file test/input/sample.md not found";
    }

    std::cout << "\n⏱️  Testing auto-close timing for Markdown\n";

    // Measure execution time
    auto start = std::chrono::high_resolution_clock::now();
    
    auto [exitCode, output] = executeViewCommand("test/input/sample.md");
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "Execution time: " << duration.count() << " seconds\n";

    // With auto-close, the command should complete within a reasonable time
    EXPECT_LT(duration.count(), 10) 
        << "View command with auto-close should complete within 10 seconds";
}

/**
 * Test viewing a non-existent file
 */
TEST_F(ViewCommandTest, ViewNonExistentFile) {
    std::cout << "\n❌ Testing: lambda view nonexistent.html\n";

    auto [exitCode, output] = executeViewCommand("nonexistent.html");

    // Should fail with non-zero exit code
    EXPECT_NE(exitCode, 0) << "View command should fail for non-existent file";

    // Output should contain error message
    EXPECT_TRUE(output.find("Error") != std::string::npos || 
                output.find("error") != std::string::npos ||
                output.find("not found") != std::string::npos ||
                output.find("No such file") != std::string::npos)
        << "Output should contain error message for non-existent file";
}

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║        Lambda View Command Test Suite                     ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Tests the 'lambda view' command with auto-close:        ║\n";
    std::cout << "║  • Viewing HTML files                                     ║\n";
    std::cout << "║  • Viewing Markdown files                                 ║\n";
    std::cout << "║  • Auto-close functionality                               ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Requirements:                                            ║\n";
    std::cout << "║  • lambda.exe built (run 'make build')                    ║\n";
    std::cout << "║  • Test files: test/html/index.html                       ║\n";
    std::cout << "║                test/input/sample.md                       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    return RUN_ALL_TESTS();
}
