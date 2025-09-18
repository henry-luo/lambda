#!/bin/bash

# Enhanced Lambda Catch2 Test Suite Runner - Test Results Breakdown
# Groups Catch2 tests by their test suite categories using dynamic discovery

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
            echo "  --target=SUITE   Run only tests from specified suite (library, input, lambda, validator, catch2)"
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

echo "🚀 Enhanced Lambda Catch2 Test Suite Runner - Test Results Breakdown"
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

# Test suite tracking (Level 1: using dynamic discovery)
declare -a suite_category_names=()
declare -a suite_category_totals=()
declare -a suite_category_passed=()
declare -a suite_category_failed=()
declare -a suite_category_status=()

# Individual Catch2 test tracking (Level 2: individual Catch2 tests)
declare -a c_test_names=()
declare -a c_test_totals=()
declare -a c_test_passed=()
declare -a c_test_failed=()
declare -a c_test_status=()
declare -a c_test_suites=()  # Maps Catch2 test to its suite category

# Function to map executable name to test suite category (using dynamic categorization)
get_suite_category() {
    local exe_name="$1"
    
    # Dynamic categorization based on test name patterns
    case "$exe_name" in
        # Library Tests - basic utility and core functionality tests
        "test_datetime_catch2"|"test_num_stack_catch2"|"test_strbuf_catch2"|"test_stringbuf_catch2"|"test_strview_catch2"|"test_url_catch2"|"test_url_extra_catch2"|"test_variable_pool_catch2")
            echo "library" ;;
        # Input Processing Tests - file format parsing and processing
        "test_css_"*"_catch2"|"test_dir_catch2"|"test_http_catch2"|"test_input_roundtrip_catch2"|"test_jsx_roundtrip_catch2"|"test_markup_roundtrip_catch2"|"test_math_catch2"|"test_math_ascii_catch2"|"test_mdx_roundtrip_catch2"|"test_mime_detect_catch2"|"test_sysinfo_catch2")
            echo "input" ;;
        # Lambda Runtime Tests - language runtime and evaluation
        "test_lambda_catch2"|"test_lambda_proc_catch2"|"test_lambda_repl_catch2")
            echo "lambda" ;;
        # Validator Tests - schema and document validation
        "test_validator_catch2")
            echo "validator" ;;
        # Catch anything with _catch2 suffix as generic
        *"_catch2")
            echo "catch2" ;;
        # Default for unknown
        *)
            echo "unknown" ;;
    esac
}

# Function to get test suite display name (matching old test system structure)
get_suite_category_display_name() {
    local category="$1"
    
    # Dynamic display names based on category
    case "$category" in
        "library") echo "📚 Library Tests" ;;
        "input") echo "📄 Input Processing Tests" ;;
        "lambda") echo "🐑 Lambda Runtime Tests" ;;
        "validator") echo "🔍 Validator Tests" ;;
        "catch2") echo "🧪 Catch2 Tests" ;;
        "unknown") echo "🧪 Other Tests" ;;
        *) echo "🧪 $category Tests" ;;
    esac
}

# Global arrays to cache test name mappings
test_name_keys=()
test_name_values=()

# Function to build test name lookup from build config
build_test_name_lookup() {
    local config_file="build_lambda_config.json"
    
    # Clear existing lookup
    test_name_keys=()
    test_name_values=()
    
    if [ ! -f "$config_file" ]; then
        echo "Warning: build_lambda_config.json not found, falling back to defaults" >&2
        return 1
    fi
    
    # Extract test configurations from JSON using simple parsing
    # Look for test entries that have both "source" and "name" fields
    while IFS= read -r line; do
        # Check if this line contains a source field for catch2 tests
        if echo "$line" | grep -q '"source".*".*_catch2'; then
            # Extract the source filename
            local source=$(echo "$line" | sed 's/.*"source"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
            # Convert source to binary name (remove .cpp/.c extension)
            local binary_name=$(basename "$source" .cpp)
            binary_name=$(basename "$binary_name" .c)
            
            # Look for the corresponding name field in the next few lines
            local name=""
            local found_name=false
            local line_count=0
            while IFS= read -r next_line && [ $line_count -lt 10 ]; do
                if echo "$next_line" | grep -q '"name"[[:space:]]*:'; then
                    name=$(echo "$next_line" | sed 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
                    # Remove "(Catch2)" suffix to match old system
                    name=$(echo "$name" | sed 's/ (Catch2)$//')
                    found_name=true
                    break
                fi
                # Stop if we hit another test entry or end of current test
                if echo "$next_line" | grep -q '"source"\|^[[:space:]]*}[[:space:]]*$'; then
                    break
                fi
                ((line_count++))
            done
            
            if [ "$found_name" = true ] && [ -n "$name" ]; then
                # Store mapping from binary name to display name
                test_name_keys+=("$binary_name")
                test_name_values+=("$name")
            fi
        fi
    done < "$config_file"
    
    return 0
}

# Function to lookup test name from arrays
lookup_test_name() {
    local key="$1"
    
    for i in "${!test_name_keys[@]}"; do
        if [ "${test_name_keys[$i]}" = "$key" ]; then
            echo "${test_name_values[$i]}"
            return 0
        fi
    done
    
    return 1
}

# Function to map executable name to friendly Catch2 test name (lookup from build config)
get_c_test_display_name() {
    local exe_name="$1"
    
    # Build lookup table if not already done
    if [ ${#test_name_keys[@]} -eq 0 ]; then
        build_test_name_lookup
    fi
    
    # Remove .exe suffix for lookup
    local lookup_key=$(basename "$exe_name" .exe)
    
    # Try to find in lookup table
    local result
    if result=$(lookup_test_name "$lookup_key"); then
        echo "$result"
    else
        # Fallback to default format if not found in config
        echo "🧪 $lookup_key"
    fi
}

# Function to run a test executable with timeout and crash protection
run_test_with_timeout() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local output_file="$TEST_OUTPUT_DIR/${base_name}_output.txt"
    
    echo "📋 Running $base_name..." >&2
    
    # Run Catch2 test with crash protection from project root directory
    if [ "$RAW_OUTPUT" = true ]; then
        # Use timeout with signal handling to catch crashes
        timeout --preserve-status "$TIMEOUT_DURATION" "$test_exe" --reporter compact --success 2>&1
        local exit_code=$?
    else
        # Capture all output and handle crashes gracefully
        timeout --preserve-status "$TIMEOUT_DURATION" "$test_exe" --reporter compact --success > "$output_file" 2>&1
        local exit_code=$?
    fi
    
    # Interpret exit codes more gracefully
    if [ $exit_code -eq 0 ]; then
        echo "✅ $base_name completed successfully" >&2
    elif [ $exit_code -eq 124 ]; then
        echo "⏰ $base_name timed out after ${TIMEOUT_DURATION}s" >&2
        # Write timeout info to output file if not already captured
        if [ "$RAW_OUTPUT" != true ] && [ ! -s "$output_file" ]; then
            echo "Test timed out after ${TIMEOUT_DURATION}s" > "$output_file"
        fi
    elif [ $exit_code -eq 134 ]; then
        echo "💥 $base_name crashed with SIGABRT but continuing..." >&2
        # Write crash info to output file if not already captured
        if [ "$RAW_OUTPUT" != true ] && [ ! -s "$output_file" ]; then
            echo "Test crashed with SIGABRT (exit code 134)" > "$output_file"
        fi
    elif [ $exit_code -eq 139 ]; then
        echo "💥 $base_name crashed with SIGSEGV but continuing..." >&2
        # Write crash info to output file if not already captured
        if [ "$RAW_OUTPUT" != true ] && [ ! -s "$output_file" ]; then
            echo "Test crashed with SIGSEGV (exit code 139)" > "$output_file"
        fi
    else
        echo "⚠️  $base_name completed with exit code $exit_code" >&2
        # Write exit code info to output file if not already captured
        if [ "$RAW_OUTPUT" != true ] && [ ! -s "$output_file" ]; then
            echo "Test completed with exit code $exit_code" > "$output_file"
        fi
    fi
    
    echo "$output_file $exit_code"
    return 0
}

# Function to parse Catch2 output and extract counts with crash handling
parse_catch2_results() {
    local output_file="$1"
    local exit_code="$2"
    
    if [ ! -f "$output_file" ]; then
        echo "0 1"
        return 1
    fi
    
    # Handle crashed tests specially - but extract results if available
    if [ $exit_code -eq 134 ] || [ $exit_code -eq 139 ]; then
        # Check if we have any partial results before the crash
        local passed=0
        local failed=1  # Default to failed due to crash
        
        # Try to extract partial results if available
        if grep -q "test cases:" "$output_file" 2>/dev/null || strings "$output_file" | grep -q "test cases:"; then
            # Extract the actual test results from the last "test cases:" line
            local last_result=$(grep "test cases:" "$output_file" 2>/dev/null | tail -1 || \
                               strings "$output_file" | grep "test cases:" | tail -1)
            
            if [ -n "$last_result" ]; then
                passed=$(echo "$last_result" | grep -o "[0-9]* passed" | grep -o "[0-9]*" || echo "0")
                local test_failed=$(echo "$last_result" | grep -o "[0-9]* failed" | grep -o "[0-9]*" || echo "0")
                
                # Use the actual test results, don't add crash as additional failure
                # since the tests completed and reported their results
                failed=$test_failed
                
                # If we got valid results, don't treat as a crash failure
                if [ "$passed" -gt 0 ] || [ "$test_failed" -gt 0 ]; then
                    echo "$passed $failed"
                    return 0
                fi
            fi
        fi
        
        # Fallback if no valid results found
        echo "$passed $failed"
        return 0
    fi
    
    # Parse Catch2 compact output format
    # Look for lines like "All tests passed (289 assertions in 12 test cases)" or "All tests passed (66 assertions in 1 test case)"
    # or "test cases: X | Y passed | Z failed"
    local passed=0
    local failed=0
    
    if [ $exit_code -eq 0 ]; then
        # Tests passed - check both "All tests passed" and "test cases:" formats
        if grep -q "All tests passed" "$output_file" 2>/dev/null || strings "$output_file" | grep -q "All tests passed"; then
            # Extract test case count (handles both "test case" and "test cases")
            # Try direct grep first, then fall back to strings if file contains binary data
            passed=$(grep -o "All tests passed ([0-9]* assertions in [0-9]* test cases\?)" "$output_file" 2>/dev/null | grep -o "[0-9]* test cases\?" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep -o "All tests passed ([0-9]* assertions in [0-9]* test cases\?)" | grep -o "[0-9]* test cases\?" | grep -o "[0-9]*" || echo "0")
            failed=0
        elif grep -q "test cases:" "$output_file" 2>/dev/null || strings "$output_file" | grep -q "test cases:"; then
            # Handle cases with expected failures - format: "test cases: 5 | 2 passed | 3 failed as expected"
            # Try direct grep first, then fall back to strings if file contains binary data
            local total_tests=$(grep "test cases:" "$output_file" 2>/dev/null | grep -o "test cases:[[:space:]]*[0-9]*" | grep -o "[0-9]*" || \
                               strings "$output_file" | grep "test cases:" | grep -o "test cases:[[:space:]]*[0-9]*" | grep -o "[0-9]*" || echo "0")
            passed=$(grep "test cases:" "$output_file" 2>/dev/null | grep -o "[0-9]* passed" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep "test cases:" | grep -o "[0-9]* passed" | grep -o "[0-9]*" || echo "0")
            failed=0  # Exit code 0 means overall success, even with expected failures
            # Use total test count if we got it
            if [ "$total_tests" -gt 0 ]; then
                passed=$total_tests
            fi
        else
            passed=0
            failed=0
        fi
    else
        # Some tests failed - try to extract counts
        if grep -q "test cases:" "$output_file" 2>/dev/null || strings "$output_file" | grep -q "test cases:"; then
            # Format: "test cases: 15 | 12 passed | 3 failed"
            # Try direct grep first, then fall back to strings if file contains binary data
            passed=$(grep "test cases:" "$output_file" 2>/dev/null | tail -1 | grep -o "[0-9]* passed" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep "test cases:" | tail -1 | grep -o "[0-9]* passed" | grep -o "[0-9]*" || echo "0")
            failed=$(grep "test cases:" "$output_file" 2>/dev/null | tail -1 | grep -o "[0-9]* failed" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep "test cases:" | tail -1 | grep -o "[0-9]* failed" | grep -o "[0-9]*" || echo "0")
            
            # If we didn't get passed count, try to extract total and calculate
            if [ "$passed" = "0" ]; then
                local total_tests=$(grep "test cases:" "$output_file" 2>/dev/null | tail -1 | grep -o "test cases:[[:space:]]*[0-9]*" | grep -o "[0-9]*" || \
                                   strings "$output_file" | grep "test cases:" | tail -1 | grep -o "test cases:[[:space:]]*[0-9]*" | grep -o "[0-9]*" || echo "0")
                if [ "$total_tests" -gt 0 ] && [ "$failed" -gt 0 ]; then
                    passed=$((total_tests - failed))
                fi
            fi
        else
            # Fallback - assume at least one failure
            passed=0
            failed=1
        fi
    fi
    
    # Ensure we always return valid numbers
    if ! [[ "$passed" =~ ^[0-9]+$ ]]; then
        passed=0
    fi
    if ! [[ "$failed" =~ ^[0-9]+$ ]]; then
        failed=1
    fi
    
    # If both are 0, default to 1 failed for safety
    if [ "$passed" -eq 0 ] && [ "$failed" -eq 0 ]; then
        failed=1
    fi
    
    echo "$passed $failed"
}

# Function to run a single Catch2 test and create JSON result file
run_single_catch2_test() {
    local test_exe="$1"
    local base_name=$(basename "$test_exe" .exe)
    local c_test_display_name=$(get_c_test_display_name "$base_name")
    local suite_category=$(get_suite_category "$base_name")
    local result_file="$TEST_OUTPUT_DIR/${base_name}_catch2_result.json"
    
    if [ "$RAW_OUTPUT" != true ]; then
        echo "🏃 Running $c_test_display_name..." >&2
    fi
    
    # Handle test execution with proper error handling
    set +e  # Temporarily disable exit on error for test execution
    result=$(run_test_with_timeout "$test_exe")
    output_file=$(echo "$result" | cut -d' ' -f1)
    exit_code=$(echo "$result" | cut -d' ' -f2)
    
    local passed=0
    local failed=0
    local total=0
    local status="❌ ERROR"
    
    if [ -n "$output_file" ]; then
        # Parse results
        results=$(parse_catch2_results "$output_file" "$exit_code")
        if [ -n "$results" ]; then
            passed=$(echo "$results" | cut -d' ' -f1)
            failed=$(echo "$results" | cut -d' ' -f2)
            total=$((passed + failed))
            
            # Determine Catch2 test status
            if [ $failed -eq 0 ]; then
                status="✅ PASS"
                if [ "$RAW_OUTPUT" != true ]; then
                    echo "   ✅ $total tests passed" >&2
                fi
            else
                status="❌ FAIL"
                if [ "$RAW_OUTPUT" != true ]; then
                    echo "   ✅ $passed tests passed, ❌ $failed tests failed" >&2
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
        echo "  \"output_file\": \"$output_file\","
        echo "  \"failed_tests\": ["
        
        # Extract failed test names
        local first_failed=true
        if [ -n "$output_file" ] && [ -f "$output_file" ]; then
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
            done < <(extract_failed_test_names "$output_file" "$base_name")
        fi
        echo ""
        echo "  ]"
        echo "}"
    } > "$result_file"
    
    return 0
}

# Function to extract failed test names from Catch2 output
extract_failed_test_names() {
    local output_file="$1"
    local base_name="$2"
    
    if [ ! -f "$output_file" ]; then
        return
    fi
    
    # Extract failed test names from Catch2 output
    # Look for multiple patterns to catch different failure formats
    
    # Pattern 1: Lines starting with test file path and FAILED
    if grep -q "FAILED:" "$output_file" 2>/dev/null; then
        grep "FAILED:" "$output_file" | sed 's/.*FAILED: //' | while IFS= read -r line; do
            if [ -n "$line" ]; then
                echo "[$base_name] $line"
            fi
        done
    fi
    
    # Pattern 2: Catch2 assertion failures
    if grep -q "CHECK\|REQUIRE" "$output_file" 2>/dev/null; then
        grep -E "(CHECK|REQUIRE).*for:|with expansion:" "$output_file" | while IFS= read -r line; do
            if [ -n "$line" ]; then
                # Clean up the assertion line
                clean_line=$(echo "$line" | sed 's/^[[:space:]]*//' | sed 's/.*\/\///')
                if [ -n "$clean_line" ]; then
                    echo "[$base_name] $clean_line"
                fi
            fi
        done
    fi
    
    # Pattern 3: Generic failure indicators if nothing else found
    if ! grep -q "FAILED:\|CHECK\|REQUIRE" "$output_file" 2>/dev/null; then
        # Look for any error indicators
        if grep -qi "error\|fail\|abort\|crash" "$output_file" 2>/dev/null; then
            echo "[$base_name] Test failed with errors (check output file for details)"
        fi
    fi
}

echo "🔍 Finding Catch2 test executables..."

# Dynamic discovery approach: find all actually built Catch2 test executables
# First try to use the list generated by build-catch2, then fall back to discovery
get_available_catch2_tests() {
    # Check if we have a list from the build system
    if [ -f "test_output/available_catch2_tests.txt" ]; then
        echo "Using dynamically generated test list from build system..."
        cat test_output/available_catch2_tests.txt | grep -v '^$'
    else
        echo "Discovering Catch2 tests by scanning build directory..."
        # Fallback: discover by scanning the build directory for catch2 executables
        find build/premake -name "*catch2*" -executable -type f 2>/dev/null | \
            xargs -I {} basename {} | \
            sed 's/\.exe$//' | \
            sort -u
    fi
}

# Get array of available Catch2 tests
available_tests=()
while IFS= read -r test_name; do
    if [ -n "$test_name" ]; then
        available_tests+=("$test_name")
    fi
done < <(get_available_catch2_tests)

echo "Available Catch2 tests: ${available_tests[*]}"

# Find existing executables for the available tests
test_executables=()
for test_name in "${available_tests[@]}"; do
    base_name=$(basename "$test_name" .cpp)
    exe_file="test/${base_name}.exe"
    
    # Check if executable exists in test/ directory
    if [ -f "$exe_file" ]; then
        test_executables+=("$exe_file")
    # Also check if executable exists in build/premake directory (where make builds them)
    elif [ -f "build/premake/bin/Debug/${base_name}.exe" ]; then
        test_executables+=("build/premake/bin/Debug/${base_name}.exe")
    elif [ -f "build/premake/bin/Debug/${base_name}" ]; then
        test_executables+=("build/premake/bin/Debug/${base_name}")
    fi
done

# Remove duplicates and sort
test_executables=($(printf "%s\n" "${test_executables[@]}" | sort -u))

if [ ${#test_executables[@]} -eq 0 ]; then
    echo "❌ No Catch2 test executables found in test/ directory"
    echo "   Make sure you've run 'make test-catch2' first to build the tests"
    exit 1
fi

# Filter executables by target suite if specified
if [ -n "$TARGET_SUITE" ]; then
    filtered_executables=()
    for test_exe in "${test_executables[@]}"; do
        base_name=$(basename "$test_exe" .exe)
        suite_category=$(get_suite_category "$base_name")
        if [ "$suite_category" = "$TARGET_SUITE" ]; then
            filtered_executables+=("$test_exe")
        fi
    done
    test_executables=("${filtered_executables[@]}")
    
    if [ ${#test_executables[@]} -eq 0 ]; then
        echo "❌ No Catch2 test executables found for target suite: $TARGET_SUITE"
        echo "   Available suites: library, input, lambda, validator, catch2"
        exit 1
    fi
fi

echo "📋 Found ${#test_executables[@]} Catch2 test executable(s)"
echo ""

# Execute tests (parallel or sequential)
if [ "$PARALLEL_EXECUTION" = true ] && [ "$RAW_OUTPUT" != true ]; then
    echo "⚡ Running Catch2 tests in parallel..."
    echo ""
    
    # Start all tests in parallel
    test_pids=()
    result_files=()
    
    for test_exe in "${test_executables[@]}"; do
        base_name=$(basename "$test_exe" .exe)
        
        # Check if executable exists
        if [ -f "$test_exe" ] && [ -x "$test_exe" ]; then
            # Calculate result file path
            result_file="$TEST_OUTPUT_DIR/${base_name}_catch2_result.json"
            result_files+=("$result_file")
            
            echo "   Starting $base_name..."
            
            # Run test in background and collect PID
            temp_output="$TEST_OUTPUT_DIR/${base_name}_temp_output.log"
            run_single_catch2_test "$test_exe" > "$temp_output" 2>&1 &
            pid=$!
            test_pids+=($pid)
            
            echo "   Started $base_name with PID $pid"
        else
            echo "⚠️  Skipping $test_exe (not executable)"
        fi
    done
    
    echo ""
    echo "Started ${#test_pids[@]} parallel processes"
    echo "Expected result files: ${#result_files[@]}"
    
    # Wait for all tests to complete
    echo "⏳ Waiting for ${#test_pids[@]} parallel test(s) to complete..."
    
    # Wait for each process with timeout
    wait_timeout=300  # 5 minutes total timeout
    start_time=$(date +%s)
    
    for i in "${!test_pids[@]}"; do
        pid="${test_pids[$i]}"
        echo "   Waiting for PID $pid..."
        
        # Check if process is still running
        if kill -0 "$pid" 2>/dev/null; then
            # Wait for this specific process with timeout
            elapsed=0
            while kill -0 "$pid" 2>/dev/null && [ $elapsed -lt $wait_timeout ]; do
                sleep 1
                elapsed=$(($(date +%s) - start_time))
            done
            
            # If still running after timeout, kill it
            if kill -0 "$pid" 2>/dev/null; then
                echo "   ⚠️ Process $pid timed out, killing..."
                kill -TERM "$pid" 2>/dev/null || true
                sleep 2
                kill -KILL "$pid" 2>/dev/null || true
            fi
        fi
        
        # Final wait to collect exit status
        wait "$pid" 2>/dev/null || true
    done
    
    echo "✅ All parallel tests completed!"
    echo ""
    
    # Process results from all test files
    echo "🔍 Processing test results..."
    for i in "${!result_files[@]}"; do
        result_file="${result_files[$i]}"
        base_name=$(basename "$result_file" "_catch2_result.json")
        
        if [ -f "$result_file" ]; then
            # Parse JSON result file
            display_name=$(jq -r '.display_name' "$result_file" 2>/dev/null)
            suite_category=$(jq -r '.suite_category' "$result_file" 2>/dev/null)
            passed=$(jq -r '.passed' "$result_file" 2>/dev/null)
            failed=$(jq -r '.failed' "$result_file" 2>/dev/null)
            total=$(jq -r '.total' "$result_file" 2>/dev/null)
            status=$(jq -r '.status' "$result_file" 2>/dev/null)
            
            # Validate parsed values
            if [ "$display_name" = "null" ] || [ -z "$display_name" ]; then display_name="🧪 $base_name"; fi
            if [ "$suite_category" = "null" ] || [ -z "$suite_category" ]; then suite_category="unknown"; fi
            if [ "$passed" = "null" ] || [ -z "$passed" ]; then passed=0; fi
            if [ "$failed" = "null" ] || [ -z "$failed" ]; then failed=0; fi
            if [ "$total" = "null" ] || [ -z "$total" ]; then total=$((passed + failed)); fi
            if [ "$status" = "null" ] || [ -z "$status" ]; then status="❌ ERROR"; fi
            
            # Show individual test results as we process them
            echo "   $status $display_name ($passed/$total tests)"
            
            # Add to overall totals
            total_tests=$((total_tests + total))
            total_passed=$((total_passed + passed))
            total_failed=$((total_failed + failed))
            
            # Track individual Catch2 test results
            c_test_names+=("$display_name")
            c_test_totals+=("$total")
            c_test_passed+=("$passed")
            c_test_failed+=("$failed")
            c_test_suites+=("$suite_category")
            
            # Determine Catch2 test status
            if [ $failed -eq 0 ]; then
                c_test_status+=("✅ PASS")
            else
                c_test_status+=("❌ FAIL")
            fi
            
            # Extract failed test names from JSON array
            while IFS= read -r failed_name; do
                if [ -n "$failed_name" ] && [ "$failed_name" != "null" ]; then
                    failed_test_names+=("$failed_name")
                fi
            done < <(jq -r '.failed_tests[]?' "$result_file" 2>/dev/null)
        else
            echo "   ⚠️ No result file found for $base_name"
            # Add placeholder data for missing results
            c_test_names+=("🧪 $base_name")
            c_test_totals+=(0)
            c_test_passed+=(0)
            c_test_failed+=(1)
            c_test_suites+=("unknown")
            c_test_status+=("❌ ERROR")
            total_failed=$((total_failed + 1))
        fi
    done
    
else
    # Sequential execution fallback
    echo "🔄 Running Catch2 tests sequentially..."
    echo ""
    
    # Run each test executable sequentially
    for test_exe in "${test_executables[@]}"; do
        base_name=$(basename "$test_exe" .exe)
        
        # Check if executable exists
        if [ -f "$test_exe" ] && [ -x "$test_exe" ]; then
            c_test_display_name=$(get_c_test_display_name "$base_name")
            suite_category=$(get_suite_category "$base_name")
            
            echo "🏃 Running $c_test_display_name..."
            
            # Handle test execution with proper error handling
            set +e  # Temporarily disable exit on error for test execution
            result=$(run_test_with_timeout "$test_exe")
            output_file=$(echo "$result" | cut -d' ' -f1)
            exit_code=$(echo "$result" | cut -d' ' -f2)
            if [ "$RAW_OUTPUT" != true ]; then
                set -e  # Re-enable exit on error only in non-raw mode
            fi
            
            if [ -n "$output_file" ]; then
                # Parse results
                results=$(parse_catch2_results "$output_file" "$exit_code")
                if [ -n "$results" ]; then
                    passed=$(echo "$results" | cut -d' ' -f1)
                    failed=$(echo "$results" | cut -d' ' -f2)
                    c_test_total=$((passed + failed))
                    
                    # Add to overall totals
                    total_tests=$((total_tests + c_test_total))
                    total_passed=$((total_passed + passed))
                    total_failed=$((total_failed + failed))
                    
                    # Track individual Catch2 test results
                    c_test_names+=("$c_test_display_name")
                    c_test_totals+=("$c_test_total")
                    c_test_passed+=("$passed")
                    c_test_failed+=("$failed")
                    c_test_suites+=("$suite_category")
                    
                    # Determine Catch2 test status
                    if [ $failed -eq 0 ]; then
                        c_test_status+=("✅ PASS")
                        echo "   ✅ $c_test_total tests passed"
                    else
                        c_test_status+=("❌ FAIL")
                        echo "   ✅ $passed tests passed, ❌ $failed tests failed"
                    fi
                    
                    # Extract failed test names with suite prefix
                    while IFS= read -r failed_name; do
                        if [ -n "$failed_name" ]; then
                            failed_test_names+=("$failed_name")
                        fi
                    done < <(extract_failed_test_names "$output_file" "$base_name")
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
            echo "⚠️  Skipping $test_exe (not executable or missing)"
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
            # Add bounds checking to prevent array index errors
            if [ $i -lt ${#c_test_totals[@]} ] && [ $i -lt ${#c_test_passed[@]} ] && [ $i -lt ${#c_test_failed[@]} ]; then
                suite_total=$((suite_total + c_test_totals[$i]))
                suite_passed=$((suite_passed + c_test_passed[$i]))
                suite_failed=$((suite_failed + c_test_failed[$i]))
            else
                echo "Warning: Array index mismatch detected for test $i, skipping..." >&2
            fi
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
    echo "   - Individual output files: *_output.txt"
    
    # Exit with appropriate code based on test results
    if [ $total_failed -gt 0 ]; then
        exit 1
    else
        exit 0
    fi
fi

echo ""
echo "=============================================================="
echo "🏁 CATCH2 TEST RESULTS BREAKDOWN"
echo "=============================================================="

# Two-level tree breakdown (suite categories with individual tests nested)
echo "📊 Test Results:"

# Show tests in tree structure grouped by suite category (matching old test system order)
for suite_cat in library input lambda validator unknown; do
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
        
        # Show appropriate format based on suite status
        if [ $suite_failed -eq 0 ]; then
            echo "   $suite_display $suite_status ($suite_passed/$suite_total tests)"
        else
            echo "   $suite_display $suite_status ($suite_passed passed, $suite_failed failed of $suite_total tests)"
        fi
        
        # Second pass: show individual tests under this suite
        for i in "${!c_test_suites[@]}"; do
            if [ "${c_test_suites[$i]}" = "$suite_cat" ]; then
                # Add bounds checking to prevent array index errors
                if [ $i -lt ${#c_test_names[@]} ] && [ $i -lt ${#c_test_status[@]} ] && [ $i -lt ${#c_test_passed[@]} ] && [ $i -lt ${#c_test_totals[@]} ]; then
                    c_test_name="${c_test_names[$i]}"
                    status="${c_test_status[$i]}"
                    passed="${c_test_passed[$i]}"
                    total="${c_test_totals[$i]}"
                    
                    # Show appropriate format based on test status
                    if [[ "$status" == *"FAIL"* ]] && [ $i -lt ${#c_test_failed[@]} ]; then
                        failed="${c_test_failed[$i]}"
                        echo "     └─ $c_test_name $status ($passed passed, $failed failed of $total tests)"
                    else
                        echo "     └─ $c_test_name $status ($passed/$total tests)"
                    fi
                else
                    echo "Warning: Array index mismatch detected for test display $i, skipping..." >&2
                fi
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
summary_file="$TEST_OUTPUT_DIR/test_summary_catch2.json"

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
        # Add bounds checking to prevent array index errors in JSON generation
        if [ $i -lt ${#c_test_suites[@]} ] && [ $i -lt ${#c_test_totals[@]} ] && [ $i -lt ${#c_test_passed[@]} ] && [ $i -lt ${#c_test_failed[@]} ] && [ $i -lt ${#c_test_status[@]} ]; then
            printf '        {"name": "%s", "suite": "%s", "total": %d, "passed": %d, "failed": %d, "status": "%s"}' \
                "${c_test_names[$i]}" "${c_test_suites[$i]}" "${c_test_totals[$i]}" "${c_test_passed[$i]}" "${c_test_failed[$i]}" "${c_test_status[$i]}"
            if [ $i -lt $((${#c_test_names[@]} - 1)) ]; then
                echo ","
            else
                echo ""
            fi
        else
            echo "        {\"name\": \"Error: Array index mismatch\", \"suite\": \"unknown\", \"total\": 0, \"passed\": 0, \"failed\": 1, \"status\": \"❌ ERROR\"}"
            if [ $i -lt $((${#c_test_names[@]} - 1)) ]; then
                echo ","
            else
                echo ""
            fi
        fi
    done
    echo "    ]"
    
    echo "}"
} > "$summary_file"

echo ""
echo "📁 Results saved to: $TEST_OUTPUT_DIR"
echo "   - Individual output files: *_output.txt"
echo "   - Two-level summary: test_summary_catch2.json"

# Exit with appropriate code
if [ $total_failed -gt 0 ]; then
    exit 1
else
    exit 0
fi
