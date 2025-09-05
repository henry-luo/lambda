#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <map>
#include <iomanip>
#include <sstream>
#include <thread>
#include <future>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

// Simple directory listing functions
std::vector<std::string> listFilesInDir(const std::string& path) {
    std::vector<std::string> files;
    DIR *dir;
    struct dirent *ent;
    struct stat st;
    
    if ((dir = opendir(path.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            std::string filename = ent->d_name;
            if (filename == "." || filename == "..") continue;
            
            std::string fullpath = path + "/" + filename;
            if (stat(fullpath.c_str(), &st) == -1) continue;
            
            if (S_ISDIR(st.st_mode)) {
                // Recursively add files from subdirectories
                auto subfiles = listFilesInDir(fullpath);
                files.insert(files.end(), subfiles.begin(), subfiles.end());
            } else if (S_ISREG(st.st_mode)) {
                files.push_back(fullpath);
            }
        }
        closedir(dir);
    }
    return files;
}

// Get file extension
std::string getFileExtension(const std::string& filename) {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        return filename.substr(dot_pos);
    }
    return "";
}

// Get filename without path and extension
std::string getStem(const std::string& path) {
    size_t last_slash = path.find_last_of("/\\");
    size_t last_dot = path.find_last_of('.');
    
    if (last_slash == std::string::npos) last_slash = 0;
    else last_slash++;  // skip the slash
    
    if (last_dot == std::string::npos || last_dot < last_slash) {
        return path.substr(last_slash);
    }
    return path.substr(last_slash, last_dot - last_slash);
}

// Helper function to trim whitespace
void trim(std::string& str) {
    // Remove leading whitespace
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    // Remove trailing whitespace
    str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), str.end());
}

// Helper function to execute lambda.exe and capture output
std::string executeLambdaScript(const std::string& file_path, int timeout_seconds = 30) {
    std::string command = "./lambda.exe " + file_path + " 2>/dev/null";
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "ERROR: Failed to execute lambda.exe";
    }
    
    std::string full_output;
    char buffer[4096];
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        full_output += buffer;
    }
    
    int exit_code = pclose(pipe);
    if (exit_code != 0) {
        return "ERROR: lambda.exe exited with code " + std::to_string(exit_code);
    }
    
    // Extract only the actual script output after the marker line
    size_t marker_pos = full_output.find("##### Script");
    if (marker_pos != std::string::npos) {
        size_t result_start = full_output.find('\n', marker_pos);
        if (result_start != std::string::npos) {
            return full_output.substr(result_start + 1);
        }
    }
    
    // If no marker found, return the full output (fallback)
    return full_output;
}

// Test result structure
struct TestResult {
    std::string name;
    std::string category;
    std::string type;  // positive, negative, boundary
    bool passed;
    std::string expected;
    std::string actual;
    std::string error_message;
    double execution_time_ms;
    std::string file_path;
};

// Test suite statistics
struct TestStats {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    int skipped_tests = 0;
    double total_time_ms = 0.0;
    std::map<std::string, int> category_counts;
    std::map<std::string, int> type_counts;
};

class LambdaTestRunner {
private:
    std::vector<TestResult> results;
    TestStats stats;
    bool verbose;
    std::string output_format; // "json", "tap", "both"
    static constexpr int DEFAULT_TIMEOUT_SECONDS = 30;

public:
    LambdaTestRunner(bool verbose = false, const std::string& format = "both") 
        : verbose(verbose), output_format(format) {
        // Runtime initialization moved to child processes
    }

    ~LambdaTestRunner() {
        // No runtime cleanup needed in main process
    }

    // Parse test metadata from Lambda script comments
    struct TestMetadata {
        std::string name;
        std::string category;
        std::string type;
        std::string expected_result;
        bool should_fail;
    };

    TestMetadata parseTestMetadata(const std::string& file_path) {
        TestMetadata meta;
        std::ifstream file(file_path);
        std::string line;
        
        // Default values
        meta.name = getStem(file_path);
        meta.category = "unknown";
        meta.type = "positive";
        meta.should_fail = false;
        
        // Parse metadata from comments at the top of the file
        while (std::getline(file, line) && line.find("//") == 0) {
            if (line.find("// Test:") == 0) {
                meta.name = line.substr(8);
                trim(meta.name);
            } else if (line.find("// Category:") == 0) {
                meta.category = line.substr(12);
                trim(meta.category);
            } else if (line.find("// Type:") == 0) {
                meta.type = line.substr(8);
                trim(meta.type);
                meta.should_fail = (meta.type == "negative");
            } else if (line.find("// Expected:") == 0) {
                meta.expected_result = line.substr(12);
                trim(meta.expected_result);
            }
        }
        
        return meta;
    }

    // Execute a single test using CLI invocation
    TestResult executeTestInProcess(const std::string& file_path, const TestMetadata& meta) {
        TestResult result;
        result.name = meta.name;
        result.category = meta.category;
        result.type = meta.type;
        result.file_path = file_path;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Execute lambda script via lambda.exe
        std::string actual_output = executeLambdaScript(file_path, DEFAULT_TIMEOUT_SECONDS);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        
        // Trim whitespace from actual output
        trim(actual_output);
        result.actual = actual_output;
        
        // Check for errors in output
        bool has_errors = (actual_output.find("ERROR") != std::string::npos || 
                          actual_output.find("error") != std::string::npos ||
                          actual_output.find("TIMEOUT") != std::string::npos);
        
        // Process results based on test expectations
        if (actual_output.find("ERROR:") == 0) {
            // Execution failed
            result.passed = meta.should_fail;
            result.error_message = actual_output;
            result.actual = has_errors ? "ERROR" : "PROCESS_ERROR";
        } else {
            // Test completed successfully
            if (meta.should_fail && !has_errors) {
                result.passed = false;
                result.error_message = "Test expected to fail but succeeded";
            } else if (meta.should_fail && has_errors) {
                result.passed = true;
                result.expected = "error";
            } else if (!meta.should_fail && has_errors) {
                result.passed = false;
                result.error_message = "Test failed with error";
            } else {
                // Load expected result if available
                std::string expected_file = file_path.substr(0, file_path.find_last_of('.')) + ".expected";
                if (std::filesystem::exists(expected_file)) {
                    result.expected = loadExpectedResult(expected_file);
                    result.passed = (result.actual == result.expected);
                    if (!result.passed) {
                        result.error_message = "Output mismatch";
                    }
                } else {
                    result.passed = true;
                    result.expected = meta.expected_result.empty() ? "success" : meta.expected_result;
                }
            }
        }
        
        return result;
    }

    // Run a single test file
    TestResult runSingleTest(const std::string& file_path) {
        TestMetadata meta = parseTestMetadata(file_path);
        return executeTestInProcess(file_path, meta);
    }

    // Discover and run all tests in a directory
    void runTestSuite(const std::string& test_dir) {
        std::vector<std::string> test_files;
        
        // Find all .ls files recursively
        auto all_files = listFilesInDir(test_dir);
        for (const auto& file : all_files) {
            if (getFileExtension(file) == ".ls") {
                test_files.push_back(file);
            }
        }
        
        std::sort(test_files.begin(), test_files.end());
        
        if (verbose) {
            std::cout << "Found " << test_files.size() << " test files\n";
        }
        
        // Run each test
        for (const auto& file : test_files) {
            if (verbose) {
                std::cout << "Running: " << file << std::endl;
            }
            
            TestResult result = runSingleTest(file);
            results.push_back(result);
            
            // Update statistics
            stats.total_tests++;
            if (result.passed) {
                stats.passed_tests++;
            } else {
                stats.failed_tests++;
            }
            stats.total_time_ms += result.execution_time_ms;
            stats.category_counts[result.category]++;
            stats.type_counts[result.type]++;
            
            if (verbose) {
                std::cout << "  " << (result.passed ? "PASS" : "FAIL") 
                         << " (" << std::fixed << std::setprecision(2) 
                         << result.execution_time_ms << "ms)" << std::endl;
            }
        }
    }

    // Generate JSON report
    void generateJsonReport(const std::string& output_file) {
        std::ofstream json_file(output_file);
        json_file << "{\n";
        json_file << "  \"summary\": {\n";
        json_file << "    \"total\": " << stats.total_tests << ",\n";
        json_file << "    \"passed\": " << stats.passed_tests << ",\n";
        json_file << "    \"failed\": " << stats.failed_tests << ",\n";
        json_file << "    \"skipped\": " << stats.skipped_tests << ",\n";
        json_file << "    \"execution_time_ms\": " << std::fixed << std::setprecision(2) << stats.total_time_ms << "\n";
        json_file << "  },\n";
        
        json_file << "  \"categories\": {\n";
        bool first = true;
        for (const auto& [category, count] : stats.category_counts) {
            if (!first) json_file << ",\n";
            json_file << "    \"" << category << "\": " << count;
            first = false;
        }
        json_file << "\n  },\n";
        
        json_file << "  \"types\": {\n";
        first = true;
        for (const auto& [type, count] : stats.type_counts) {
            if (!first) json_file << ",\n";
            json_file << "    \"" << type << "\": " << count;
            first = false;
        }
        json_file << "\n  },\n";
        
        json_file << "  \"tests\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            json_file << "    {\n";
            json_file << "      \"name\": \"" << escapeJson(result.name) << "\",\n";
            json_file << "      \"category\": \"" << result.category << "\",\n";
            json_file << "      \"type\": \"" << result.type << "\",\n";
            json_file << "      \"file\": \"" << escapeJson(result.file_path) << "\",\n";
            json_file << "      \"passed\": " << (result.passed ? "true" : "false") << ",\n";
            json_file << "      \"execution_time_ms\": " << std::fixed << std::setprecision(2) << result.execution_time_ms << ",\n";
            json_file << "      \"expected\": \"" << escapeJson(result.expected) << "\",\n";
            json_file << "      \"actual\": \"" << escapeJson(result.actual) << "\",\n";
            json_file << "      \"error_message\": \"" << escapeJson(result.error_message) << "\"\n";
            json_file << "    }";
            if (i < results.size() - 1) json_file << ",";
            json_file << "\n";
        }
        json_file << "  ]\n";
        json_file << "}\n";
    }

    // Generate TAP (Test Anything Protocol) report
    void generateTapReport(const std::string& output_file) {
        std::ofstream tap_file(output_file);
        
        tap_file << "TAP version 13\n";
        tap_file << "1.." << stats.total_tests << "\n";
        
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            
            if (result.passed) {
                tap_file << "ok " << (i + 1) << " - " << result.name;
            } else {
                tap_file << "not ok " << (i + 1) << " - " << result.name;
            }
            
            // Add metadata
            tap_file << " # category:" << result.category 
                    << " type:" << result.type 
                    << " time:" << std::fixed << std::setprecision(2) << result.execution_time_ms << "ms";
            
            if (!result.passed && !result.error_message.empty()) {
                tap_file << " error:" << result.error_message;
            }
            
            tap_file << "\n";
            
            // Add diagnostic information for failures
            if (!result.passed) {
                tap_file << "  ---\n";
                tap_file << "  message: \"" << result.error_message << "\"\n";
                tap_file << "  severity: fail\n";
                tap_file << "  data:\n";
                tap_file << "    got: \"" << escapeYaml(result.actual) << "\"\n";
                tap_file << "    expect: \"" << escapeYaml(result.expected) << "\"\n";
                tap_file << "    file: \"" << result.file_path << "\"\n";
                tap_file << "  ...\n";
            }
        }
        
        // Summary comment
        tap_file << "# Summary: " << stats.passed_tests << " passed, " 
                << stats.failed_tests << " failed, " 
                << stats.total_tests << " total\n";
        tap_file << "# Total execution time: " << std::fixed << std::setprecision(2) 
                << stats.total_time_ms << "ms\n";
    }

    // Print summary to console
    void printSummary() {
        std::cout << "\n=== Test Summary ===\n";
        std::cout << "Total tests: " << stats.total_tests << "\n";
        std::cout << "Passed: " << stats.passed_tests << "\n";
        std::cout << "Failed: " << stats.failed_tests << "\n";
        std::cout << "Success rate: " << std::fixed << std::setprecision(1) 
                 << (stats.total_tests > 0 ? (double)stats.passed_tests / stats.total_tests * 100 : 0) << "%\n";
        std::cout << "Total time: " << std::fixed << std::setprecision(2) << stats.total_time_ms << "ms\n";
        
        if (stats.failed_tests > 0) {
            std::cout << "\nFailed tests:\n";
            for (const auto& result : results) {
                if (!result.passed) {
                    std::cout << "  - " << result.name << " (" << result.file_path << ")\n";
                    std::cout << "    Error: " << result.error_message << "\n";
                }
            }
        }
    }

private:
    std::string loadExpectedResult(const std::string& file_path) {
        std::ifstream file(file_path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        trim(content);
        return content;
    }

    std::string escapeJson(const std::string& str) {
        std::string escaped;
        for (char c : str) {
            switch (c) {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += c; break;
            }
        }
        return escaped;
    }

    std::string escapeYaml(const std::string& str) {
        std::string escaped;
        for (char c : str) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else escaped += c;
        }
        return escaped;
    }
};

int main(int argc, char* argv[]) {
    std::string test_dir = "test/std";
    std::string output_format = "both";
    std::string json_output = "test_output/lambda_test_runner_results.json";
    std::string tap_output = "test_output/lambda_test_runner_results.tap";
    bool verbose = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--format" && i + 1 < argc) {
            output_format = argv[++i];
        } else if (arg == "--test-dir" && i + 1 < argc) {
            test_dir = argv[++i];
        } else if (arg == "--json-output" && i + 1 < argc) {
            json_output = argv[++i];
        } else if (arg == "--tap-output" && i + 1 < argc) {
            tap_output = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Lambda Test Runner\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --test-dir <dir>     Test directory (default: test/std)\n";
            std::cout << "  --format <format>    Output format: json, tap, both (default: both)\n";
            std::cout << "  --json-output <file> JSON output file (default: test_output/lambda_test_runner_results.json)\n";
            std::cout << "  --tap-output <file>  TAP output file (default: test_output/lambda_test_runner_results.tap)\n";
            std::cout << "  --verbose, -v        Verbose output\n";
            std::cout << "  --help, -h           Show this help\n";
            return 0;
        }
    }
    
    // Create test_output directory if it doesn't exist
    std::filesystem::create_directories("test_output");
    
    LambdaTestRunner runner(verbose, output_format);
    
    std::cout << "Lambda Test Runner\n";
    std::cout << "Test directory: " << test_dir << "\n";
    std::cout << "Output format: " << output_format << "\n\n";
    
    // Run the test suite
    runner.runTestSuite(test_dir);
    
    // Generate reports
    if (output_format == "json" || output_format == "both") {
        runner.generateJsonReport(json_output);
        std::cout << "JSON report written to: " << json_output << "\n";
    }
    
    if (output_format == "tap" || output_format == "both") {
        runner.generateTapReport(tap_output);
        std::cout << "TAP report written to: " << tap_output << "\n";
    }
    
    // Print summary
    runner.printSummary();
    
    return 0;
}
