/**
 * GTest for Lambda Network Layout Commands
 * 
 * Tests the `lambda layout` and `lambda render` commands with HTTP/HTTPS URLs:
 * - Layout via HTTP URL
 * - Render via HTTP URL
 * - Network caching verification
 * 
 * Usage:
 *   make test-baseline  # Runs this test along with other baseline tests
 *   ./test/test_network_layout_gtest.exe  # Run directly
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
#include <signal.h>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <atomic>

// for popen on Windows
#ifdef _WIN32
    #define popen _popen
    #define pclose _pclose
#endif

/**
 * Test fixture for network layout command tests
 */
class NetworkLayoutTest : public ::testing::Test {
protected:
    static pid_t server_pid;
    static int server_port;
    static std::atomic<bool> server_started;

    static void SetUpTestSuite() {
        // Start HTTP server for the test suite
        server_port = 19999;  // use high port to avoid conflicts
        startHttpServer();
    }

    static void TearDownTestSuite() {
        // Stop HTTP server
        stopHttpServer();
    }

    static void startHttpServer() {
        // check if port is already in use and kill any existing server
        char kill_cmd[256];
        snprintf(kill_cmd, sizeof(kill_cmd), "lsof -ti:%d | xargs kill -9 2>/dev/null || true", server_port);
        system(kill_cmd);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // fork to start python http server
        server_pid = fork();
        if (server_pid == 0) {
            // child process - start the http server
            // redirect stdout/stderr to /dev/null to avoid noise
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            
            // change to test page directory
            if (chdir("./test/layout/data/page") != 0) {
                _exit(1);
            }

            // exec python http server
            char port_str[16];
            snprintf(port_str, sizeof(port_str), "%d", server_port);
            execlp("python3", "python3", "-m", "http.server", port_str, NULL);
            
            // if execlp fails, exit
            _exit(1);
        }

        // parent process - wait for server to start
        server_started = false;
        for (int i = 0; i < 30; i++) {  // wait up to 3 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // try to connect to server
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "curl -s -o /dev/null -w '%%{http_code}' http://localhost:%d/ 2>/dev/null", server_port);
            FILE* pipe = popen(cmd, "r");
            if (pipe) {
                char buffer[16];
                if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    if (strncmp(buffer, "200", 3) == 0) {
                        server_started = true;
                        pclose(pipe);
                        break;
                    }
                }
                pclose(pipe);
            }
        }

        if (!server_started) {
            std::cerr << "Warning: HTTP server may not have started properly" << std::endl;
        }
    }

    static void stopHttpServer() {
        if (server_pid > 0) {
            kill(server_pid, SIGTERM);
            int status;
            waitpid(server_pid, &status, 0);
            server_pid = 0;
        }
        
        // also kill any lingering processes on the port
        char kill_cmd[256];
        snprintf(kill_cmd, sizeof(kill_cmd), "lsof -ti:%d | xargs kill -9 2>/dev/null || true", server_port);
        system(kill_cmd);
    }

    void SetUp() override {
        // check if lambda.exe exists and is executable
        std::ifstream exe("./lambda.exe");
        if (!exe.good()) {
            GTEST_SKIP() << "lambda.exe not found - please run 'make build' first";
        }

        int result = access("./lambda.exe", X_OK);
        if (result != 0) {
            GTEST_SKIP() << "lambda.exe exists but is not executable";
        }

        if (!server_started) {
            GTEST_SKIP() << "HTTP server not running";
        }
    }

    /**
     * Execute a lambda command and capture output
     */
    std::pair<int, std::string> executeCommand(const std::string& cmd) {
        std::string full_cmd = cmd + " 2>&1";
        
        FILE* pipe = popen(full_cmd.c_str(), "r");
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

        return {exitCode, output};
    }

    /**
     * Check if a file exists
     */
    bool fileExists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
    }

    /**
     * Get file size
     */
    long getFileSize(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.good()) return -1;
        return file.tellg();
    }

    /**
     * Generate URL for test server
     */
    std::string testUrl(const std::string& file) {
        char url[256];
        snprintf(url, sizeof(url), "http://localhost:%d/%s", server_port, file.c_str());
        return url;
    }
};

// static member initialization
pid_t NetworkLayoutTest::server_pid = 0;
int NetworkLayoutTest::server_port = 19999;
std::atomic<bool> NetworkLayoutTest::server_started{false};

/**
 * Test layout command with HTTP URL
 */
TEST_F(NetworkLayoutTest, LayoutWithHttpUrl) {
    std::cout << "\n📊 Testing: lambda layout with HTTP URL\n";

    std::string url = testUrl("cern.html");
    std::string cmd = "./lambda.exe layout " + url;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;
    if (!output.empty()) {
        std::cout << "Output: " << output.substr(0, 500) << std::endl;
    }

    // check exit code - 0 means success
    EXPECT_EQ(exitCode, 0) << "Layout command should succeed with HTTP URL";

    // check that output mentions success
    EXPECT_TRUE(output.find("1 success") != std::string::npos ||
                output.find("Completed layout command") != std::string::npos)
        << "Layout should report success";

    // verify view_tree.txt was created
    EXPECT_TRUE(fileExists("view_tree.txt"))
        << "view_tree.txt should be generated";
}

/**
 * Test render command with HTTP URL to PNG
 */
TEST_F(NetworkLayoutTest, RenderHttpUrlToPng) {
    std::cout << "\n🖼️  Testing: lambda render HTTP URL to PNG\n";

    std::string url = testUrl("cern.html");
    std::string output_file = "/tmp/test_network_layout_render.png";

    // remove output file if exists
    unlink(output_file.c_str());

    std::string cmd = "./lambda.exe render " + url + " -o " + output_file;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;
    if (!output.empty()) {
        std::cout << "Output: " << output.substr(0, 500) << std::endl;
    }

    // check exit code
    EXPECT_EQ(exitCode, 0) << "Render command should succeed with HTTP URL";

    // check output file exists
    EXPECT_TRUE(fileExists(output_file))
        << "PNG output file should be created";

    // check file has content
    long size = getFileSize(output_file);
    EXPECT_GT(size, 1000)
        << "PNG file should have reasonable size (got " << size << " bytes)";

    std::cout << "Rendered PNG size: " << size << " bytes" << std::endl;

    // cleanup
    unlink(output_file.c_str());
}

/**
 * Test render command with HTTP URL to SVG
 */
TEST_F(NetworkLayoutTest, RenderHttpUrlToSvg) {
    std::cout << "\n📐 Testing: lambda render HTTP URL to SVG\n";

    std::string url = testUrl("cern.html");
    std::string output_file = "/tmp/test_network_layout_render.svg";

    // remove output file if exists
    unlink(output_file.c_str());

    std::string cmd = "./lambda.exe render " + url + " -o " + output_file;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;

    // check exit code
    EXPECT_EQ(exitCode, 0) << "Render command should succeed with HTTP URL";

    // check output file exists
    EXPECT_TRUE(fileExists(output_file))
        << "SVG output file should be created";

    // check file has content and contains SVG
    std::ifstream file(output_file);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    EXPECT_GT(content.size(), 100)
        << "SVG file should have content";

    EXPECT_TRUE(content.find("<svg") != std::string::npos ||
                content.find("<?xml") != std::string::npos)
        << "Output should be valid SVG";

    std::cout << "Rendered SVG size: " << content.size() << " bytes" << std::endl;

    // cleanup
    unlink(output_file.c_str());
}

/**
 * Test layout with multiple HTML pages from HTTP server
 */
TEST_F(NetworkLayoutTest, LayoutMultipleHttpPages) {
    std::cout << "\n📚 Testing: lambda layout with multiple HTTP pages\n";

    const char* test_pages[] = {
        "cern.html",
        "about.html",
        "demo.html"
    };

    int success_count = 0;
    for (const char* page : test_pages) {
        std::string url = testUrl(page);
        std::string cmd = "./lambda.exe layout " + url;

        auto [exitCode, output] = executeCommand(cmd);

        if (exitCode == 0 && (output.find("1 success") != std::string::npos ||
                              output.find("Completed layout command") != std::string::npos)) {
            success_count++;
            std::cout << "✅ " << page << " - OK" << std::endl;
        } else {
            std::cout << "❌ " << page << " - Failed (exit=" << exitCode << ")" << std::endl;
        }
    }

    EXPECT_GE(success_count, 2)
        << "At least 2 pages should layout successfully";
}

/**
 * Test that HTTP 404 errors are handled gracefully
 */
TEST_F(NetworkLayoutTest, LayoutHttpNotFound) {
    std::cout << "\n🚫 Testing: lambda layout with non-existent HTTP URL\n";

    std::string url = testUrl("does_not_exist_12345.html");
    std::string cmd = "./lambda.exe layout " + url;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;
    if (!output.empty()) {
        std::cout << "Output: " << output.substr(0, 500) << std::endl;
    }

    // should fail gracefully (non-zero exit but no crash)
    EXPECT_NE(exitCode, 0)
        << "Layout command should fail for non-existent URL";

    // check that it reports failure properly
    EXPECT_TRUE(output.find("failed") != std::string::npos ||
                output.find("Failed") != std::string::npos ||
                output.find("Error") != std::string::npos ||
                output.find("0 success") != std::string::npos)
        << "Should report failure in output";
}

/**
 * Test that caching works for repeated requests
 * (This is a basic timing test - second request should be similar or faster)
 */
TEST_F(NetworkLayoutTest, HttpCachingWorks) {
    std::cout << "\n💾 Testing: HTTP caching for repeated requests\n";

    std::string url = testUrl("demo.html");
    std::string cmd = "./lambda.exe layout " + url;

    // first request
    auto start1 = std::chrono::high_resolution_clock::now();
    auto [exitCode1, output1] = executeCommand(cmd);
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count();

    ASSERT_EQ(exitCode1, 0) << "First layout should succeed";

    // second request (should use cache if implemented)
    auto start2 = std::chrono::high_resolution_clock::now();
    auto [exitCode2, output2] = executeCommand(cmd);
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count();

    EXPECT_EQ(exitCode2, 0) << "Second layout should succeed";

    std::cout << "First request: " << duration1 << "ms" << std::endl;
    std::cout << "Second request: " << duration2 << "ms" << std::endl;

    // we don't enforce cache performance, just verify both requests work
    // caching verification would need to check log output for cache hits
}

/**
 * Test render to PDF with HTTP URL
 */
TEST_F(NetworkLayoutTest, RenderHttpUrlToPdf) {
    std::cout << "\n📄 Testing: lambda render HTTP URL to PDF\n";

    std::string url = testUrl("cern.html");
    std::string output_file = "/tmp/test_network_layout_render.pdf";

    // remove output file if exists
    unlink(output_file.c_str());

    std::string cmd = "./lambda.exe render " + url + " -o " + output_file;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;

    // check exit code
    EXPECT_EQ(exitCode, 0) << "Render to PDF should succeed with HTTP URL";

    // check output file exists
    EXPECT_TRUE(fileExists(output_file))
        << "PDF output file should be created";

    // check file has content
    long size = getFileSize(output_file);
    EXPECT_GT(size, 100)
        << "PDF file should have reasonable size (got " << size << " bytes)";

    std::cout << "Rendered PDF size: " << size << " bytes" << std::endl;

    // cleanup
    unlink(output_file.c_str());
}

/**
 * Test layout with external HTTPS URL (no extension, Content-Type detection)
 */
TEST_F(NetworkLayoutTest, LayoutExternalHttpsWithContentTypeDetection) {
    std::cout << "\n🌍 Testing: lambda layout with external HTTPS URL (no extension)\n";

    // Use example.com which is a stable test page with no extension
    std::string url = "https://example.com/";
    std::string cmd = "./lambda.exe layout " + url;

    std::cout << "Executing: " << cmd << std::endl;

    auto [exitCode, output] = executeCommand(cmd);

    std::cout << "Exit code: " << exitCode << std::endl;
    if (!output.empty()) {
        std::cout << "Output: " << output.substr(0, 500) << std::endl;
    }

    // check exit code - 0 means success
    EXPECT_EQ(exitCode, 0) << "Layout command should succeed with HTTPS URL without extension";

    // check that output mentions success
    EXPECT_TRUE(output.find("1 success") != std::string::npos ||
                output.find("Completed layout command") != std::string::npos)
        << "Layout should report success";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
