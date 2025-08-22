#!/bin/bash

# Enhanced Lambda Test Suite Runner - Test Results Breakdown
# Groups C-level tests by their test suite categories from build_lambda_config.json

set -e

# Source test build utilities for compilation
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/test_build.sh" ]; then
    source "$SCRIPT_DIR/test_build.sh"
else
    echo "Warning: test_build.sh not found, tests will not be automatically recompiled"
fi

# Parse command line arguments
TARGET_SUITE=""
RAW_OUTPUT=false
PARALLEL_EXECUTION=true  # Default to parallel execution

while [[ $# -gt 0 ]]; do
    case $1 in
        --target=*)
            TARGET_SUITE="${1#*=}"
            shift
            ;;
        --target)
            TARGET_SUITE="$2"
            shift 2
            ;;
        --raw)
            RAW_OUTPUT=true
            shift
            ;;
        --sequential)
            PARALLEL_EXECUTION=false
            shift
            ;;
        --parallel)
            PARALLEL_EXECUTION=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--target=SUITE] [--raw] [--sequential] [--parallel]"
            echo "  --target=SUITE   Run only tests from specified suite (library, input, mir, lambda, validator)"
            echo "  --raw           Show raw test output without formatting"
            echo "  --sequential    Run tests sequentially (default: parallel)"
            echo "  --parallel      Run tests in parallel (default)"
            echo "  --help          Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo "🚀 Enhanced Lambda Test Suite Runner - Test Results Breakdown"
if [ -n "$TARGET_SUITE" ]; then
    echo "🎯 Target Suite: $TARGET_SUITE"
fi
if [ "$RAW_OUTPUT" = true ]; then
    echo "🔧 Raw Output Mode: Test failures will not stop execution"
    set +e  # Disable exit on error in raw mode to see all test output
fi
if [ "$PARALLEL_EXECUTION" = true ]; then
    echo "⚡ Parallel Execution: Running test suites concurrently"
else
    echo "🔄 Sequential Execution: Running test suites one by one"
fi
echo "=============================================================="

# Configuration
TIMEOUT_DURATION="60s"
TEST_OUTPUT_DIR="test_output"

# Create output directory
mkdir -p "$TEST_OUTPUT_DIR"

# Initialize counters
total_tests=0
total_passed=0
total_failed=0
failed_test_names=()

# Test suite tracking (Level 1: from build_lambda_config.json)
declare -a suite_category_names=()
declare -a suite_category_totals=()
declare -a suite_category_passed=()
declare -a suite_category_failed=()
declare -a suite_category_status=()

# Individual C test tracking (Level 2: individual C tests)
declare -a c_test_names=()
declare -a c_test_totals=()
declare -a c_test_passed=()
declare -a c_test_failed=()
declare -a c_test_status=()
declare -a c_test_suites=()  # Maps C test to its suite category

# Function to map executable name to test suite category (from build_lambda_config.json)
get_test_suite_category() {
    local exe_name="$1"
    case "$exe_name" in
        # Library suite tests
        "test_strbuf"|"test_strview"|"test_variable_pool"|"test_num_stack"|"test_datetime"|"test_url"|"test_url_extra") 
            echo "library" ;;
        # Input suite tests  
        "test_mime_detect"|"test_math"|"test_markup_roundtrip")
            echo "input" ;;
        # MIR suite tests
        "test_mir")
            echo "mir" ;;
        # Lambda suite tests
        "test_lambda")
            echo "lambda" ;;
        # Validator suite tests
        "test_validator")
            echo "validator" ;;
        *)
            echo "unknown" ;;
    esac
}

# Function to get test suite display name (from build_lambda_config.json)
get_suite_category_display_name() {
    local category="$1"
    case "$category" in
        "library") echo "📚 Library Tests" ;;
        "input") echo "📄 Input Processing Tests" ;;
        "mir") echo "⚡ MIR JIT Tests" ;;
        "lambda") echo "🐑 Lambda Runtime Tests" ;;
        "validator") echo "🔍 Validator Tests" ;;
        "unknown") echo "🧪 Other Tests" ;;
        *) echo "🧪 $category Tests" ;;
    esac
}

# Function to map executable name to friendly C test name
get_c_test_display_name() {
    local exe_name="$1"
    case "$exe_name" in
        "test_datetime") echo "📅 DateTime Tests" ;;
        "test_lambda") echo "🐑 Lambda Runtime Tests" ;;
        "test_markup_roundtrip") echo "📝 Markup Roundtrip Tests" ;;
        "test_math") echo "🔢 Math Roundtrip Tests" ;;
        "test_mime_detect") echo "📎 MIME Detection Tests" ;;
        "test_mir") echo "⚡ MIR JIT Tests" ;;
        "test_num_stack") echo "🔢 Number Stack Tests" ;;
        "test_strbuf") echo "📝 String Buffer Tests" ;;
        "test_strview") echo "👀 String View Tests" ;;
        "test_url_extra") echo "🌐 URL Extra Tests" ;;
        "test_url") echo "🔗 URL Tests" ;;
        "test_validator") echo "🔍 Validator Tests" ;;
        "test_variable_pool") echo "🏊 Variable Pool Tests" ;;
        *) echo "🧪 $exe_name" ;;
    esac
}

# Function to check if test needs recompilation and build if necessary
ensure_test_compiled() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local source_file="test/${base_name}.c"
    
    # Check if source file exists
    if [ ! -f "$source_file" ]; then
        return 0  # No source file, assume executable is fine
    fi
    
    # Check if executable exists and is newer than source
    if [ -f "$test_exe" ] && [ "$test_exe" -nt "$source_file" ]; then
        return 0  # Executable is up to date
    fi
    
    # Need to recompile
    echo "🔄 Recompiling $base_name (source newer than executable)..." >&2
    
    # Get the test suite category to determine compilation parameters
    local suite_category=$(get_test_suite_category "$base_name")
    
    # Use build_test function from test_build.sh if available
    if declare -f build_test >/dev/null 2>&1; then
        if build_test "$suite_category" "$source_file" "$test_exe"; then
            echo "✅ Successfully recompiled $base_name" >&2
            return 0
        else
            echo "❌ Failed to recompile $base_name" >&2
            return 1
        fi
    else
        echo "❌ build_test function not available (test_build.sh not sourced?)" >&2
        return 1
    fi
}

# Function to run a test executable with timeout and JSON output
run_test_with_timeout() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local json_file="$TEST_OUTPUT_DIR/${base_name}_results.json"
    
    echo "📋 Running $base_name..." >&2
    
    # Run test with timeout
    if [ "$RAW_OUTPUT" = true ]; then
        # Raw mode: show output directly without JSON redirect
        timeout "$TIMEOUT_DURATION" "./$test_exe" --json="$json_file"
        local exit_code=$?
    else
        # Normal mode: capture output for processing
        timeout "$TIMEOUT_DURATION" "./$test_exe" --json="$json_file" >/dev/null 2>&1
        local exit_code=$?
    fi
    
    if [ $exit_code -eq 0 ]; then
        echo "✅ $base_name completed successfully" >&2
    else
        if [ $exit_code -eq 124 ]; then
            echo "⏰ $base_name timed out after ${TIMEOUT_DURATION}s" >&2
            echo '{"passed": 0, "failed": 1, "tests": [{"name": "timeout_error", "status": "failed", "message": "Test timed out"}]}' > "$json_file"
        else
            echo "⚠️  $base_name completed with exit code $exit_code" >&2
        fi
    fi
    
    # Check if JSON file was created and is valid
    if [ -f "$json_file" ] && jq empty "$json_file" 2>/dev/null; then
        echo "$json_file"
        return 0
    else
        echo "❌ No valid JSON output from $base_name" >&2
        # Create a minimal error JSON file for consistency
        echo '{"passed": 0, "failed": 1, "tests": [{"name": "json_error", "status": "failed", "message": "No valid JSON output"}]}' > "$json_file"
        echo "$json_file"
        return 1
    fi
}

# Function to parse JSON results and extract counts
parse_json_results() {
    local json_file="$1"
    
    if [ ! -f "$json_file" ]; then
        return 1
    fi
    
    # Extract counts using jq
    local passed=$(jq -r '.passed // 0' "$json_file" 2>/dev/null || echo "0")
    local failed=$(jq -r '.failed // 0' "$json_file" 2>/dev/null || echo "0")
    
    echo "$passed $failed"
}

# Function to run a single test executable and return results
run_single_test() {
    local test_exe="$1"
    local base_name=$(basename "$test_exe" .exe)
    local c_test_display_name=$(get_c_test_display_name "$base_name")
    local suite_category=$(get_test_suite_category "$base_name")
    local result_file="$TEST_OUTPUT_DIR/${base_name}_test_result.json"
    
    # Ensure test is compiled and up to date
    if ! ensure_test_compiled "$test_exe"; then
        echo "❌ Failed to compile $base_name, skipping test" >&2
        # Create a minimal error JSON file for consistency
        echo '{"passed": 0, "failed": 1, "tests": [{"name": "compilation_error", "status": "failed", "message": "Test compilation failed"}]}' > "$result_file"
        echo "0 1 ❌ ERROR $c_test_display_name $suite_category"
        return 1
    fi
    
    if [ "$RAW_OUTPUT" != true ]; then
        echo "🏃 Running $c_test_display_name..." >&2
    fi
    
    # Handle test execution with proper error handling
    set +e  # Temporarily disable exit on error for test execution
    json_file=$(run_test_with_timeout "$test_exe")
    test_exit_code=$?
    
    local passed=0
    local failed=0
    local total=0
    local status="❌ ERROR"
    
    if [ -n "$json_file" ]; then
        # Parse results
        results=$(parse_json_results "$json_file")
        if [ -n "$results" ]; then
            passed=$(echo "$results" | cut -d' ' -f1)
            failed=$(echo "$results" | cut -d' ' -f2)
            total=$((passed + failed))
            
            # Determine C test status
            if [ $failed -eq 0 ]; then
                status="✅ PASS"
                if [ "$RAW_OUTPUT" != true ]; then
                    echo "   ✅ $total tests passed" >&2
                fi
            else
                status="❌ FAIL"
                if [ "$RAW_OUTPUT" != true ]; then
                    echo "   ❌ $failed/$total tests failed" >&2
                fi
            fi
        else
            # No valid results
            status="⚠️ NO OUTPUT"
            if [ "$RAW_OUTPUT" != true ]; then
                echo "   ⚠️ No valid test results" >&2
            fi
        fi
    else
        # Test execution failed
        failed=1
        total=1
        status="❌ ERROR"
        if [ "$RAW_OUTPUT" != true ]; then
            echo "   ❌ Test execution failed" >&2
        fi
    fi
    
    # Create JSON result file
    {
        echo "{"
        echo "  \"test_exe\": \"$test_exe\","
        echo "  \"base_name\": \"$base_name\","
        echo "  \"display_name\": \"$c_test_display_name\","
        echo "  \"suite_category\": \"$suite_category\","
        echo "  \"passed\": $passed,"
        echo "  \"failed\": $failed,"
        echo "  \"total\": $total,"
        echo "  \"status\": \"$status\","
        echo "  \"json_file\": \"$json_file\","
        echo "  \"failed_tests\": ["
        
        # Extract failed test names
        local first_failed=true
        if [ -n "$json_file" ] && [ -f "$json_file" ]; then
            while IFS= read -r failed_name; do
                if [ -n "$failed_name" ]; then
                    if [ "$first_failed" = true ]; then
                        echo -n "    \"[$base_name] $failed_name\""
                        first_failed=false
                    else
                        echo ","
                        echo -n "    \"[$base_name] $failed_name\""
                    fi
                fi
            done < <(extract_failed_test_names "$json_file")
        fi
        echo ""
        echo "  ]"
        echo "}"
    } > "$result_file"
    
    echo "$result_file"
}

# Function to extract failed test names
extract_failed_test_names() {
    local json_file="$1"
    
    if [ ! -f "$json_file" ]; then
        return
    fi
    
    # Extract failed test names using jq - handle Criterion JSON structure
    jq -r '.test_suites[]?.tests[]? | select(.status == "FAILED") | .name' "$json_file" 2>/dev/null || true
}

echo "🔍 Finding test executables and sources..."

# Find existing executables
test_executables=($(find test -name "test_*.exe" -type f 2>/dev/null | sort))

# Also find source files that might need compilation
test_sources=($(find test -name "test_*.c" -type f 2>/dev/null | sort))

# Create a list of expected executables from source files
expected_executables=()
for source_file in "${test_sources[@]}"; do
    base_name=$(basename "$source_file" .c)
    exe_file="test/${base_name}.exe"
    expected_executables+=("$exe_file")
done

# Combine and deduplicate the list of test executables
all_test_executables=($(printf "%s\n" "${test_executables[@]}" "${expected_executables[@]}" | sort -u))

test_executables=("${all_test_executables[@]}")

if [ ${#test_executables[@]} -eq 0 ]; then
    echo "❌ No test executables found in test/ directory"
    echo "   Make sure you've run ./test/test_all.sh first to build the tests"
    exit 1
fi

# Filter executables by target suite if specified
if [ -n "$TARGET_SUITE" ]; then
    filtered_executables=()
    for test_exe in "${test_executables[@]}"; do
        base_name=$(basename "$test_exe" .exe)
        suite_category=$(get_test_suite_category "$base_name")
        if [ "$suite_category" = "$TARGET_SUITE" ]; then
            filtered_executables+=("$test_exe")
        fi
    done
    test_executables=("${filtered_executables[@]}")
    
    if [ ${#test_executables[@]} -eq 0 ]; then
        echo "❌ No test executables found for target suite: $TARGET_SUITE"
        echo "   Available suites: library, input, mir, lambda, validator"
        exit 1
    fi
fi

echo "📋 Found ${#test_executables[@]} test executable(s)"
echo ""

# Execute tests (parallel or sequential)
if [ "$PARALLEL_EXECUTION" = true ] && [ "$RAW_OUTPUT" != true ]; then
    echo "⚡ Running tests in parallel..."
    echo ""
    
    # Start all tests in parallel
    test_pids=()
    result_files=()
    
    for test_exe in "${test_executables[@]}"; do
        if [ -x "$test_exe" ]; then
            # Calculate result file path
            base_name=$(basename "$test_exe" .exe)
            result_file="$TEST_OUTPUT_DIR/${base_name}_test_result.json"
            result_files+=("$result_file")
            
            # Run test in background and collect PID
            run_single_test "$test_exe" &
            test_pids+=($!)
        else
            echo "⚠️  Skipping non-executable: $test_exe"
        fi
    done
    
    # Wait for all tests to complete
    echo "⏳ Waiting for ${#test_pids[@]} parallel test(s) to complete..."
    for pid in "${test_pids[@]}"; do
        wait $pid
    done
    echo "✅ All parallel tests completed!"
    echo ""
    
    # Process results from all test files
    for result_file in "${result_files[@]}"; do
        if [ -f "$result_file" ]; then
            # Parse JSON result file
            display_name=$(jq -r '.display_name' "$result_file")
            suite_category=$(jq -r '.suite_category' "$result_file")
            passed=$(jq -r '.passed' "$result_file")
            failed=$(jq -r '.failed' "$result_file")
            total=$(jq -r '.total' "$result_file")
            status=$(jq -r '.status' "$result_file")
            
            # Add to overall totals
            total_tests=$((total_tests + total))
            total_passed=$((total_passed + passed))
            total_failed=$((total_failed + failed))
            
            # Track individual C test results
            c_test_names+=("$display_name")
            c_test_totals+=("$total")
            c_test_passed+=("$passed")
            c_test_failed+=("$failed")
            c_test_suites+=("$suite_category")
            c_test_status+=("$status")
            
            # Extract failed test names from JSON array
            while IFS= read -r failed_name; do
                if [ -n "$failed_name" ] && [ "$failed_name" != "null" ]; then
                    failed_test_names+=("$failed_name")
                fi
            done < <(jq -r '.failed_tests[]?' "$result_file")
            
            # Clean up temporary result file
            rm -f "$result_file"
        fi
    done
else
    # Sequential execution (original logic) or raw mode
    if [ "$RAW_OUTPUT" = true ]; then
        echo "🔧 Running tests sequentially in raw mode..."
    else
        echo "🔄 Running tests sequentially..."
    fi
    echo ""
    
    # Run each test executable sequentially
    for test_exe in "${test_executables[@]}"; do
        if [ -x "$test_exe" ]; then
            base_name=$(basename "$test_exe" .exe)
            c_test_display_name=$(get_c_test_display_name "$base_name")
            suite_category=$(get_test_suite_category "$base_name")
            
            echo "🏃 Running $c_test_display_name..."
            
            # Handle test execution with proper error handling
            set +e  # Temporarily disable exit on error for test execution
            json_file=$(run_test_with_timeout "$test_exe")
            test_exit_code=$?
            if [ "$RAW_OUTPUT" != true ]; then
                set -e  # Re-enable exit on error only in non-raw mode
            fi
            
            if [ -n "$json_file" ]; then
                # Parse results
                results=$(parse_json_results "$json_file")
                if [ -n "$results" ]; then
                    passed=$(echo "$results" | cut -d' ' -f1)
                    failed=$(echo "$results" | cut -d' ' -f2)
                    c_test_total=$((passed + failed))
                    
                    # Add to overall totals
                    total_tests=$((total_tests + c_test_total))
                    total_passed=$((total_passed + passed))
                    total_failed=$((total_failed + failed))
                    
                    # Track individual C test results
                    c_test_names+=("$c_test_display_name")
                    c_test_totals+=("$c_test_total")
                    c_test_passed+=("$passed")
                    c_test_failed+=("$failed")
                    c_test_suites+=("$suite_category")
                    
                    # Determine C test status
                    if [ $failed -eq 0 ]; then
                        c_test_status+=("✅ PASS")
                        echo "   ✅ $c_test_total tests passed"
                    else
                        c_test_status+=("❌ FAIL")
                        echo "   ❌ $failed/$c_test_total tests failed"
                    fi
                    
                    # Extract failed test names with suite prefix
                    while IFS= read -r failed_name; do
                        if [ -n "$failed_name" ]; then
                            failed_test_names+=("[$base_name] $failed_name")
                        fi
                    done < <(extract_failed_test_names "$json_file")
                else
                    # No valid results
                    c_test_names+=("$c_test_display_name")
                    c_test_totals+=(0)
                    c_test_passed+=(0)
                    c_test_failed+=(0)
                    c_test_suites+=("$suite_category")
                    c_test_status+=("⚠️ NO OUTPUT")
                    echo "   ⚠️ No valid test results"
                fi
            else
                # Test execution failed completely
                c_test_names+=("$c_test_display_name")
                c_test_totals+=(1)
                c_test_passed+=(0)
                c_test_failed+=(1)
                c_test_suites+=("$suite_category")
                c_test_status+=("❌ ERROR")
                echo "   ❌ Test execution failed"
            fi
        else
            echo "⚠️  Skipping non-executable: $test_exe"
        fi
    done
fi

# Group tests by suite category and calculate suite totals
echo ""
echo "📊 Calculating suite category totals..."

# Initialize arrays for unique suite categories
suite_categories=()
for i in "${!c_test_suites[@]}"; do
    suite="${c_test_suites[$i]}"
    # Check if suite is already in categories array
    found=false
    for existing_suite in "${suite_categories[@]}"; do
        if [ "$existing_suite" = "$suite" ]; then
            found=true
            break
        fi
    done
    if [ "$found" = false ]; then
        suite_categories+=("$suite")
    fi
done

# Calculate totals for each suite category
for suite in "${suite_categories[@]}"; do
    suite_display_name=$(get_suite_category_display_name "$suite")
    suite_total=0
    suite_passed=0
    suite_failed=0
    
    for i in "${!c_test_suites[@]}"; do
        if [ "${c_test_suites[$i]}" = "$suite" ]; then
            suite_total=$((suite_total + c_test_totals[$i]))
            suite_passed=$((suite_passed + c_test_passed[$i]))
            suite_failed=$((suite_failed + c_test_failed[$i]))
        fi
    done
    
    suite_category_names+=("$suite_display_name")
    suite_category_totals+=("$suite_total")
    suite_category_passed+=("$suite_passed")
    suite_category_failed+=("$suite_failed")
    
    if [ $suite_failed -eq 0 ]; then
        suite_category_status+=("✅ PASS")
    else
        suite_category_status+=("❌ FAIL")
    fi
done

# Skip summary in raw mode
if [ "$RAW_OUTPUT" = true ]; then
    echo ""
    echo "📁 Raw output mode - detailed results saved to: $TEST_OUTPUT_DIR"
    echo "   - Individual JSON results: *_results.json"
    
    # Exit with appropriate code based on test results
    if [ $total_failed -gt 0 ]; then
        exit 1
    else
        exit 0
    fi
fi

echo ""
echo "=============================================================="
echo "🏁 TEST RESULTS BREAKDOWN"
echo "=============================================================="

# Two-level tree breakdown (suite categories with individual tests nested)
echo "📊 Test Results:"

# Show tests in tree structure grouped by suite category
for suite_cat in library input mir lambda validator unknown; do
    suite_display=$(get_suite_category_display_name "$suite_cat")
    suite_has_tests=false
    suite_total=0
    suite_passed=0
    suite_failed=0
    
    # First pass: calculate suite totals
    for i in "${!c_test_suites[@]}"; do
        if [ "${c_test_suites[$i]}" = "$suite_cat" ]; then
            suite_total=$((suite_total + c_test_totals[$i]))
            suite_passed=$((suite_passed + c_test_passed[$i]))
            suite_failed=$((suite_failed + c_test_failed[$i]))
        fi
    done
    
    # Only show suite if it has tests
    if [ $suite_total -gt 0 ]; then
        # Determine suite status
        if [ $suite_failed -eq 0 ]; then
            suite_status="✅ PASS"
        else
            suite_status="❌ FAIL"
        fi
        
        echo "   $suite_display $suite_status ($suite_passed/$suite_total tests)"
        
        # Second pass: show individual tests under this suite
        for i in "${!c_test_suites[@]}"; do
            if [ "${c_test_suites[$i]}" = "$suite_cat" ]; then
                c_test_name="${c_test_names[$i]}"
                status="${c_test_status[$i]}"
                passed="${c_test_passed[$i]}"
                total="${c_test_totals[$i]}"
                
                echo "     └─ $c_test_name $status ($passed/$total tests)"
            fi
        done
    fi
done

echo ""
echo "📊 Overall Results:"
echo "   Total Tests: $total_tests"
echo "   ✅ Passed:   $total_passed"
if [ $total_failed -gt 0 ]; then
    echo "   ❌ Failed:   $total_failed"
fi

if [ $total_failed -gt 0 ]; then
    echo ""
    echo "🔍 Failed Tests:"
    for failed_name in "${failed_test_names[@]}"; do
        echo "   ❌ $failed_name"
    done
fi

echo "=============================================================="

# Save summary to file with two-level breakdown
summary_file="$TEST_OUTPUT_DIR/test_summary.json"

{
    echo "{"
    echo "    \"total_tests\": $total_tests,"
    echo "    \"total_passed\": $total_passed,"
    echo "    \"total_failed\": $total_failed,"
    echo -n "    \"failed_test_names\": ["
    if [ ${#failed_test_names[@]} -gt 0 ]; then
        printf '"%s"' "${failed_test_names[0]}"
        for (( i=1; i<${#failed_test_names[@]}; i++ )); do
            printf ',"%s"' "${failed_test_names[$i]}"
        done
    fi
    echo "],"
    
    echo "    \"level1_test_suites\": ["
    for i in "${!suite_category_names[@]}"; do
        printf '        {"name": "%s", "total": %d, "passed": %d, "failed": %d, "status": "%s"}' \
            "${suite_category_names[$i]}" "${suite_category_totals[$i]}" "${suite_category_passed[$i]}" "${suite_category_failed[$i]}" "${suite_category_status[$i]}"
        if [ $i -lt $((${#suite_category_names[@]} - 1)) ]; then
            echo ","
        else
            echo ""
        fi
    done
    echo "    ],"
    
    echo "    \"level2_c_tests\": ["
    for i in "${!c_test_names[@]}"; do
        printf '        {"name": "%s", "suite": "%s", "total": %d, "passed": %d, "failed": %d, "status": "%s"}' \
            "${c_test_names[$i]}" "${c_test_suites[$i]}" "${c_test_totals[$i]}" "${c_test_passed[$i]}" "${c_test_failed[$i]}" "${c_test_status[$i]}"
        if [ $i -lt $((${#c_test_names[@]} - 1)) ]; then
            echo ","
        else
            echo ""
        fi
    done
    echo "    ]"
    
    echo "}"
} > "$summary_file"

echo ""
echo "📁 Results saved to: $TEST_OUTPUT_DIR"
echo "   - Individual JSON results: *_results.json"
echo "   - Two-level summary: test_summary.json"

# Exit with appropriate code
if [ $total_failed -gt 0 ]; then
    exit 1
else
    exit 0
fi
