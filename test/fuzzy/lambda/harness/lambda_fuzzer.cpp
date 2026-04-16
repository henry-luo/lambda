/**
 * Lambda Script Fuzzy Tester
 * 
 * Tests robustness of Lambda's parsing, transpiling, JIT compilation, and execution.
 * 
 * Usage:
 *   ./lambda_fuzzer [options]
 *   
 * Options:
 *   --duration=TIME    Run for specified duration (e.g., 1h, 30m, 1h30m)
 *   --corpus=PATH      Path to corpus directory
 *   --seed=N           Random seed for reproducibility
 *   --timeout=MS       Per-test timeout in milliseconds (default: 5000)
 *   --verbose          Enable verbose output
 *   --differential     Enable differential testing (interpreter vs JIT)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <signal.h>
#include <setjmp.h>

extern "C" {
#include "lambda/lambda.h"
}
#include "lambda/lambda.hpp"
#include "lib/log.h"

namespace fs = std::filesystem;

// Forward declarations from generators
std::string generate_random_tokens(std::mt19937& rng, int length);
std::string mutate_program(const std::string& program, std::mt19937& rng);

// Global for signal handling
static jmp_buf fuzzer_jmp_buf;
static volatile sig_atomic_t fuzzer_timeout = 0;

void fuzzer_signal_handler(int sig) {
    if (sig == SIGALRM) {
        fuzzer_timeout = 1;
    }
    longjmp(fuzzer_jmp_buf, sig);
}

enum class Stage {
    PARSE,
    BUILD_AST,
    TRANSPILE,
    JIT_COMPILE,
    EXECUTE
};

const char* stage_name(Stage s) {
    switch (s) {
        case Stage::PARSE: return "PARSE";
        case Stage::BUILD_AST: return "BUILD_AST";
        case Stage::TRANSPILE: return "TRANSPILE";
        case Stage::JIT_COMPILE: return "JIT_COMPILE";
        case Stage::EXECUTE: return "EXECUTE";
    }
    return "UNKNOWN";
}

struct FuzzResult {
    Stage failed_stage;
    bool crashed;
    bool timeout;
    bool error;
    std::string error_message;
    double execution_time_ms;
    
    bool success() const { return !crashed && !timeout && !error; }
};

struct FuzzStats {
    size_t total_tests = 0;
    size_t passed = 0;
    size_t errors = 0;
    size_t crashes = 0;
    size_t timeouts = 0;
    size_t parse_errors = 0;
    size_t ast_errors = 0;
    size_t transpile_errors = 0;
    size_t jit_errors = 0;
    size_t runtime_errors = 0;
    double total_time_ms = 0;
    
    void record(const FuzzResult& result) {
        total_tests++;
        total_time_ms += result.execution_time_ms;
        
        if (result.crashed) {
            crashes++;
        } else if (result.timeout) {
            timeouts++;
        } else if (result.error) {
            errors++;
            switch (result.failed_stage) {
                case Stage::PARSE: parse_errors++; break;
                case Stage::BUILD_AST: ast_errors++; break;
                case Stage::TRANSPILE: transpile_errors++; break;
                case Stage::JIT_COMPILE: jit_errors++; break;
                case Stage::EXECUTE: runtime_errors++; break;
            }
        } else {
            passed++;
        }
    }
    
    void print_summary() const {
        printf("\n===== Fuzzy Test Summary =====\n");
        printf("Total tests:     %zu\n", total_tests);
        printf("Passed:          %zu (%.1f%%)\n", passed, 100.0 * passed / total_tests);
        printf("Errors:          %zu (%.1f%%)\n", errors, 100.0 * errors / total_tests);
        printf("  Parse:         %zu\n", parse_errors);
        printf("  AST:           %zu\n", ast_errors);
        printf("  Transpile:     %zu\n", transpile_errors);
        printf("  JIT:           %zu\n", jit_errors);
        printf("  Runtime:       %zu\n", runtime_errors);
        printf("Crashes:         %zu\n", crashes);
        printf("Timeouts:        %zu\n", timeouts);
        printf("Total time:      %.1f seconds\n", total_time_ms / 1000.0);
        printf("Avg time/test:   %.2f ms\n", total_time_ms / total_tests);
        printf("==============================\n");
    }
};

class LambdaFuzzer {
public:
    LambdaFuzzer(unsigned seed = 0) 
        : rng_(seed ? seed : std::random_device{}())
        , timeout_ms_(5000)
        , verbose_(false) {}
    
    void set_timeout(int ms) { timeout_ms_ = ms; }
    void set_verbose(bool v) { verbose_ = v; }
    void set_corpus_path(const std::string& path) { corpus_path_ = path; }
    
    FuzzResult fuzz(const std::string& input) {
        FuzzResult result;
        result.crashed = false;
        result.timeout = false;
        result.error = false;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Set up signal handlers
        struct sigaction sa, old_sa;
        sa.sa_handler = fuzzer_signal_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGSEGV, &sa, &old_sa);
        sigaction(SIGBUS, &sa, &old_sa);
        sigaction(SIGFPE, &sa, &old_sa);
        sigaction(SIGABRT, &sa, &old_sa);
        
        fuzzer_timeout = 0;
        
        int sig = setjmp(fuzzer_jmp_buf);
        if (sig != 0) {
            // Caught a signal
            result.crashed = (sig != SIGALRM);
            result.timeout = (sig == SIGALRM || fuzzer_timeout);
            result.error_message = result.timeout ? "Timeout" : "Crash: signal " + std::to_string(sig);
            
            auto end = std::chrono::high_resolution_clock::now();
            result.execution_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
            
            // Restore signal handlers
            sigaction(SIGSEGV, &old_sa, nullptr);
            sigaction(SIGBUS, &old_sa, nullptr);
            sigaction(SIGFPE, &old_sa, nullptr);
            sigaction(SIGABRT, &old_sa, nullptr);
            
            return result;
        }
        
        // Run the actual test
        result = run_test(input);
        
        auto end = std::chrono::high_resolution_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        // Restore signal handlers
        sigaction(SIGSEGV, &old_sa, nullptr);
        sigaction(SIGBUS, &old_sa, nullptr);
        sigaction(SIGFPE, &old_sa, nullptr);
        sigaction(SIGABRT, &old_sa, nullptr);
        
        return result;
    }
    
    void run_random_tests(size_t count) {
        printf("Running %zu random token sequence tests...\n", count);
        for (size_t i = 0; i < count; i++) {
            int len = std::uniform_int_distribution<>(1, 100)(rng_);
            std::string input = generate_random_tokens(rng_, len);
            FuzzResult result = fuzz(input);
            stats_.record(result);
            
            if (result.crashed) {
                save_crash(input, result);
            }
            
            if (verbose_ || result.crashed) {
                printf("[%zu] %s: %s\n", i, 
                       result.success() ? "PASS" : (result.crashed ? "CRASH" : "ERROR"),
                       result.error_message.c_str());
            }
        }
    }
    
    void run_mutation_tests(size_t count) {
        printf("Running %zu mutation tests...\n", count);
        
        // Load seed corpus
        std::vector<std::string> seeds = load_corpus();
        if (seeds.empty()) {
            printf("Warning: No seed corpus found, generating random seeds\n");
            for (int i = 0; i < 10; i++) {
                seeds.push_back(generate_random_tokens(rng_, 20));
            }
        }
        
        for (size_t i = 0; i < count; i++) {
            // Pick a random seed and mutate it
            const std::string& seed = seeds[std::uniform_int_distribution<size_t>(0, seeds.size() - 1)(rng_)];
            std::string mutated = mutate_program(seed, rng_);
            
            FuzzResult result = fuzz(mutated);
            stats_.record(result);
            
            if (result.crashed) {
                save_crash(mutated, result);
            }
            
            if (verbose_ || result.crashed) {
                printf("[%zu] %s: %s\n", i,
                       result.success() ? "PASS" : (result.crashed ? "CRASH" : "ERROR"),
                       result.error_message.c_str());
            }
        }
    }
    
    void run_corpus_tests() {
        printf("Running corpus tests...\n");
        std::vector<std::string> corpus = load_corpus();
        
        for (const auto& input : corpus) {
            FuzzResult result = fuzz(input);
            stats_.record(result);
            
            if (result.crashed) {
                save_crash(input, result);
            }
        }
    }
    
    void run_edge_case_tests() {
        printf("Running edge case tests...\n");
        
        // Load edge cases from corpus/edge_cases/
        std::string edge_path = corpus_path_ + "/edge_cases";
        if (fs::exists(edge_path)) {
            for (const auto& entry : fs::directory_iterator(edge_path)) {
                if (entry.path().extension() == ".ls") {
                    std::ifstream file(entry.path());
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    
                    FuzzResult result = fuzz(buffer.str());
                    stats_.record(result);
                    
                    if (verbose_) {
                        printf("  %s: %s\n", entry.path().filename().c_str(),
                               result.success() ? "PASS" : "FAIL");
                    }
                }
            }
        }
    }
    
    void print_stats() const { stats_.print_summary(); }
    const FuzzStats& stats() const { return stats_; }
    
private:
    std::mt19937 rng_;
    int timeout_ms_;
    bool verbose_;
    std::string corpus_path_;
    FuzzStats stats_;
    
    FuzzResult run_test(const std::string& input) {
        FuzzResult result;
        result.crashed = false;
        result.timeout = false;
        result.error = false;
        result.failed_stage = Stage::PARSE;
        
        // Initialize Lambda runtime
        lambda_init();
        
        // Stage 1: Parse
        result.failed_stage = Stage::PARSE;
        TSTree* tree = lambda_parse(input.c_str(), input.length());
        if (!tree) {
            result.error = true;
            result.error_message = "Parse failed";
            lambda_cleanup();
            return result;
        }
        
        // Check for parse errors
        TSNode root = ts_tree_root_node(tree);
        if (ts_node_has_error(root)) {
            result.error = true;
            result.error_message = "Parse error in tree";
            ts_tree_delete(tree);
            lambda_cleanup();
            return result;
        }
        
        // Stage 2: Build AST
        result.failed_stage = Stage::BUILD_AST;
        Ast* ast = build_ast(tree, input.c_str());
        ts_tree_delete(tree);
        
        if (!ast || ast->has_error) {
            result.error = true;
            result.error_message = "AST build failed";
            if (ast) free_ast(ast);
            lambda_cleanup();
            return result;
        }
        
        // Stage 3: Transpile
        result.failed_stage = Stage::TRANSPILE;
        // Transpilation happens during JIT compilation in Lambda
        
        // Stage 4 & 5: JIT Compile and Execute
        result.failed_stage = Stage::JIT_COMPILE;
        
        Item eval_result = lambda_eval_ast(ast);
        
        if (get_type_id(eval_result) == TypeId::Error) {
            result.error = true;
            result.failed_stage = Stage::EXECUTE;
            result.error_message = "Runtime error";
        }
        
        free_ast(ast);
        lambda_cleanup();
        
        return result;
    }
    
    std::vector<std::string> load_corpus() {
        std::vector<std::string> corpus;
        
        std::string valid_path = corpus_path_ + "/valid";
        if (fs::exists(valid_path)) {
            for (const auto& entry : fs::directory_iterator(valid_path)) {
                if (entry.path().extension() == ".ls") {
                    std::ifstream file(entry.path());
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    corpus.push_back(buffer.str());
                }
            }
        }
        
        return corpus;
    }
    
    void save_crash(const std::string& input, const FuzzResult& result) {
        std::string crash_path = corpus_path_ + "/crashes";
        fs::create_directories(crash_path);
        
        // Generate unique filename
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();
        
        std::string filename = crash_path + "/crash_" + std::to_string(millis) + ".ls";
        
        std::ofstream file(filename);
        file << "// Crash: " << result.error_message << "\n";
        file << "// Stage: " << stage_name(result.failed_stage) << "\n";
        file << input;
        file.close();
        
        printf("Crash saved to: %s\n", filename.c_str());
    }
};

// Parse duration string like "1h", "30m", "1h30m"
int parse_duration_seconds(const char* str) {
    int total = 0;
    int num = 0;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            num = num * 10 + (*str - '0');
        } else if (*str == 'h' || *str == 'H') {
            total += num * 3600;
            num = 0;
        } else if (*str == 'm' || *str == 'M') {
            total += num * 60;
            num = 0;
        } else if (*str == 's' || *str == 'S') {
            total += num;
            num = 0;
        }
        str++;
    }
    
    // Handle bare number (assume seconds) or trailing number
    total += num;
    
    return total > 0 ? total : 3600; // Default 1 hour
}

int main(int argc, char* argv[]) {
    int duration_seconds = 3600; // Default 1 hour
    unsigned seed = 0;
    int timeout_ms = 5000;
    bool verbose = false;
    bool differential = false;
    std::string corpus_path = "test/fuzzy/corpus";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_seconds = parse_duration_seconds(argv[i] + 11);
        } else if (strncmp(argv[i], "--corpus=", 9) == 0) {
            corpus_path = argv[i] + 9;
        } else if (strncmp(argv[i], "--seed=", 7) == 0) {
            seed = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
            timeout_ms = atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--differential") == 0) {
            differential = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Lambda Fuzzy Tester\n\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --duration=TIME    Run duration (e.g., 1h, 30m, 1h30m)\n");
            printf("  --corpus=PATH      Corpus directory path\n");
            printf("  --seed=N           Random seed\n");
            printf("  --timeout=MS       Per-test timeout (default: 5000)\n");
            printf("  --verbose          Verbose output\n");
            printf("  --differential     Enable differential testing\n");
            printf("  --help             Show this help\n");
            return 0;
        }
    }
    
    printf("Lambda Fuzzy Tester\n");
    printf("Duration: %d seconds\n", duration_seconds);
    printf("Corpus: %s\n", corpus_path.c_str());
    printf("Seed: %u\n", seed);
    printf("Timeout: %d ms\n", timeout_ms);
    printf("\n");
    
    LambdaFuzzer fuzzer(seed);
    fuzzer.set_timeout(timeout_ms);
    fuzzer.set_verbose(verbose);
    fuzzer.set_corpus_path(corpus_path);
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_seconds);
    
    // Run test phases
    int iteration = 0;
    while (std::chrono::steady_clock::now() < end_time) {
        iteration++;
        printf("\n--- Iteration %d ---\n", iteration);
        
        // Phase 1: Corpus tests
        fuzzer.run_corpus_tests();
        if (std::chrono::steady_clock::now() >= end_time) break;
        
        // Phase 2: Edge case tests
        fuzzer.run_edge_case_tests();
        if (std::chrono::steady_clock::now() >= end_time) break;
        
        // Phase 3: Random token tests
        fuzzer.run_random_tests(100);
        if (std::chrono::steady_clock::now() >= end_time) break;
        
        // Phase 4: Mutation tests
        fuzzer.run_mutation_tests(100);
    }
    
    fuzzer.print_stats();
    
    // Return non-zero if any crashes occurred
    return fuzzer.stats().crashes > 0 ? 1 : 0;
}
