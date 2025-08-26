#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
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

// Include Lambda runtime headers
extern "C" {
#include <tree_sitter/api.h>
#include <mpdecimal.h>
#include "../lambda/lambda.h"
#include "../lib/strbuf.h"
#include "../lib/num_stack.h"
}
#include "../lambda/transpiler.hpp"

// Implement missing functions locally to avoid linking conflicts
extern "C" Context* create_test_context() {
    Context* ctx = (Context*)calloc(1, sizeof(Context));
    if (!ctx) return NULL;
    
    // Initialize basic context fields
    ctx->decimal_ctx = (mpd_context_t*)malloc(sizeof(mpd_context_t));
    if (ctx->decimal_ctx) {
        mpd_defaultcontext(ctx->decimal_ctx);
    }
    
    // Initialize num_stack and heap to avoid crashes
    ctx->num_stack = num_stack_create(1024);  // Create with reasonable initial capacity
    ctx->heap = NULL;  // Will be initialized by heap_init()
    
    return ctx;
}

// Tree-sitter function declarations
extern "C" const TSLanguage *tree_sitter_lambda(void);

extern "C" TSParser* lambda_parser(void) {
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_lambda());
    return parser;
}

extern "C" TSTree* lambda_parse_source(TSParser* parser, const char* source_code) {
    TSTree* tree = ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));
    return tree;
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
        meta.name = std::filesystem::path(file_path).stem().string();
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

    // Execute a single test in a child process with timeout
    TestResult executeTestInProcess(const std::string& file_path, const TestMetadata& meta) {
        TestResult result;
        result.name = meta.name;
        result.category = meta.category;
        result.type = meta.type;
        result.file_path = file_path;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Create pipes for communication
        int stdout_pipe[2], stderr_pipe[2];
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
            result.passed = false;
            result.error_message = "Failed to create pipes";
            return result;
        }
        
        pid_t pid = fork();
        if (pid == -1) {
            result.passed = false;
            result.error_message = "Failed to fork process";
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
            return result;
        }
        
        if (pid == 0) {
            // Child process
            close(stdout_pipe[0]); // Close read end
            close(stderr_pipe[0]); // Close read end
            
            // Redirect stdout and stderr to pipes
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stdout_pipe[1]);
            close(stderr_pipe[1]);
            
            // Execute the test
            try {
                Runtime child_runtime;
                runtime_init(&child_runtime);
                
                Item ret = run_script_at(&child_runtime, const_cast<char*>(file_path.c_str()), false);
                
                // Format and output the result
                StrBuf* output_buf = strbuf_new_cap(1024);
                format_item(output_buf, ret, 0, const_cast<char*>(" "));
                std::cout << output_buf->str << std::flush;
                strbuf_free(output_buf);
                
                runtime_cleanup(&child_runtime);
                exit(0);
            } catch (const std::exception& e) {
                std::cerr << "ERROR: " << e.what() << std::flush;
                exit(1);
            } catch (...) {
                std::cerr << "ERROR: Unknown exception" << std::flush;
                exit(2);
            }
        } else {
            // Parent process
            close(stdout_pipe[1]); // Close write end
            close(stderr_pipe[1]); // Close write end
            
            // Wait for child with timeout
            int status;
            bool timed_out = false;
            
            // Use a separate thread to handle timeout
            std::future<int> wait_future = std::async(std::launch::async, [pid]() {
                int status;
                waitpid(pid, &status, 0);
                return status;
            });
            
            if (wait_future.wait_for(std::chrono::seconds(DEFAULT_TIMEOUT_SECONDS)) == std::future_status::timeout) {
                // Timeout occurred
                kill(pid, SIGKILL);
                wait_future.wait(); // Wait for the kill to complete
                timed_out = true;
            } else {
                status = wait_future.get();
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            
            // Read output from pipes
            char buffer[4096];
            std::string stdout_output, stderr_output;
            
            // Read stdout
            ssize_t bytes_read;
            while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0';
                stdout_output += buffer;
            }
            
            // Read stderr
            while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0';
                stderr_output += buffer;
            }
            
            // Filter Lambda output to get just the final result
            std::string filtered_output = filterLambdaOutput(stdout_output);
            
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
            
            // Process results
            if (timed_out) {
                result.passed = (meta.expected_result == "timeout");
                result.error_message = "Test timed out after " + std::to_string(DEFAULT_TIMEOUT_SECONDS) + " seconds";
                result.actual = "TIMEOUT";
                result.expected = meta.expected_result.empty() ? "success" : meta.expected_result;
            } else if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                result.actual = filtered_output;
                // Trim whitespace from actual output
                trim(result.actual);
                
                if (exit_code == 0) {
                    // Test completed successfully
                    bool has_errors = (stderr_output.find("ERROR") != std::string::npos || 
                                     stdout_output.find("error") != std::string::npos);
                    
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
                } else {
                    // Test crashed or failed
                    result.passed = meta.should_fail;
                    result.error_message = "Process exited with code " + std::to_string(exit_code);
                    result.actual = stderr_output.empty() ? "PROCESS_ERROR" : stderr_output;
                }
            } else if (WIFSIGNALED(status)) {
                // Process was killed by signal
                int signal_num = WTERMSIG(status);
                result.passed = meta.should_fail;
                result.error_message = "Process killed by signal " + std::to_string(signal_num);
                result.actual = "SIGNAL_" + std::to_string(signal_num);
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
        
        // Recursively find all .ls files
        for (const auto& entry : std::filesystem::recursive_directory_iterator(test_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ls") {
                test_files.push_back(entry.path().string());
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
    // Filter Lambda output to extract just the final result
    std::string filterLambdaOutput(const std::string& output) {
        std::istringstream stream(output);
        std::string line;
        std::string result;
        
        // Look for the last line that matches a simple result pattern
        while (std::getline(stream, line)) {
            // Skip debug lines
            if (line.find("TRACE:") != std::string::npos ||
                line.find("loading") != std::string::npos ||
                line.find("Loading") != std::string::npos ||
                line.find("Start") != std::string::npos ||
                line.find("parsing") != std::string::npos ||
                line.find("Syntax") != std::string::npos ||
                line.find("build") != std::string::npos ||
                line.find("pushing") != std::string::npos ||
                line.find("Debug:") != std::string::npos ||
                line.find("building") != std::string::npos ||
                line.find("AST:") != std::string::npos ||
                line.find("transpiling") != std::string::npos ||
                line.find("transpiled") != std::string::npos ||
                line.find("compiling") != std::string::npos ||
                line.find("C2MIR") != std::string::npos ||
                line.find("finding") != std::string::npos ||
                line.find("2025-") != std::string::npos ||
                line.find("generated") != std::string::npos ||
                line.find("init") != std::string::npos ||
                line.find("JIT") != std::string::npos ||
                line.find("jit_context") != std::string::npos ||
                line.find("loaded") != std::string::npos ||
                line.find("Executing") != std::string::npos ||
                line.find("runner") != std::string::npos ||
                line.find("heap") != std::string::npos ||
                line.find("exec") != std::string::npos ||
                line.find("list_fill") != std::string::npos ||
                line.find("entering") != std::string::npos ||
                line.find("frame_end") != std::string::npos ||
                line.find("free") != std::string::npos ||
                line.find("reached") != std::string::npos ||
                line.find("reset") != std::string::npos ||
                line.find("after") != std::string::npos ||
                line.find("#####") != std::string::npos ||
                line.find("utf8proc") != std::string::npos ||
                line.find("(") != std::string::npos ||
                line.find("[") != std::string::npos ||
                line.find("took") != std::string::npos ||
                line.find("ms") != std::string::npos ||
                line.empty()) {
                continue;
            }
            
            // Check if this looks like a result (number, simple value)
            std::string trimmed = line;
            trim(trimmed);
            if (!trimmed.empty() && 
                (std::isdigit(trimmed[0]) || trimmed[0] == '-' || 
                 trimmed == "inf" || trimmed == "null" || trimmed == "error" ||
                 trimmed == "true" || trimmed == "false" ||
                 trimmed.find("e+") != std::string::npos || trimmed.find("e-") != std::string::npos)) {
                result = trimmed;
            }
        }
        
        return result;
    }

    std::string loadExpectedResult(const std::string& file_path) {
        std::ifstream file(file_path);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        trim(content);
        return content;
    }

    void trim(std::string& str) {
        str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        str.erase(std::find_if(str.rbegin(), str.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), str.end());
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
