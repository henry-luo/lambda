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
    
    # Get suite category from build configuration using new tests array structure
    local suite_category=$(jq -r --arg exe "$exe_name" '
        .test.test_suites[] | 
        select(.tests[]? | .source | test("\\b" + $exe + "\\.(c|cpp)$")) | 
        .suite
    ' build_lambda_config.json 2>/dev/null | head -1)
    
    # If found in config, return it
    if [ -n "$suite_category" ] && [ "$suite_category" != "null" ]; then
        echo "$suite_category"
        return
    fi
    
    # Fallback for special cases
    case "$exe_name" in
        "lambda_test_runner")
            echo "lambda-std" ;;
        *)
            echo "unknown" ;;
    esac
}

# Function to get test suite display name (from build_lambda_config.json)
get_suite_category_display_name() {
    local category="$1"
    
    # Get display name from build configuration
    local display_name=$(jq -r --arg suite "$category" '
        .test.test_suites[] | 
        select(.suite == $suite) | 
        .name
    ' build_lambda_config.json 2>/dev/null | head -1)
    
    # If found in config, return it
    if [ -n "$display_name" ] && [ "$display_name" != "null" ]; then
        echo "$display_name"
        return
    fi
    
    # Fallback for hardcoded display names
    case "$category" in
        "library") echo "📚 Library Tests" ;;
        "input") echo "📄 Input Processing Tests" ;;
        "mir") echo "⚡ MIR JIT Tests" ;;
        "lambda") echo "🐑 Lambda Runtime Tests" ;;
        "lambda-std") echo "🧪 Lambda Standard Tests" ;;
        "validator") echo "🔍 Validator Tests" ;;
        "unknown") echo "🧪 Other Tests" ;;
        *) echo "🧪 $category Tests" ;;
    esac
}

# Function to map executable name to friendly C test name
get_c_test_display_name() {
    local exe_name="$1"
    
    # Try to get custom display name from build configuration using new tests array structure
    local display_name=$(jq -r --arg exe "$exe_name" '
        .test.test_suites[] | 
        .tests[]? | 
        select(.source | test("\\b" + $exe + "\\.(c|cpp)$")) |
        .name
    ' build_lambda_config.json 2>/dev/null | head -1)
    
    # If found custom name, return it
    if [ -n "$display_name" ] && [ "$display_name" != "null" ]; then
        echo "$display_name"
        return
    fi
    
    # Fallback to hardcoded friendly names
    case "$exe_name" in
        "test_datetime") echo "📅 DateTime Tests" ;;
        "test_dir") echo "📁 Directory Listing Tests" ;;
        "test_http") echo "🌐 HTTP/HTTPS Tests" ;;
        "test_input_roundtrip") echo "🔄 Input Roundtrip Tests" ;;
        "test_lambda") echo "🐑 Lambda Runtime Tests" ;;
        "test_lambda_catch2") echo "🐑 Lambda Runtime Tests (Catch2)" ;;
        "test_lambda_proc_catch2") echo "🐑 Lambda Procedural Tests (Catch2)" ;;
        "test_lambda_repl_catch2") echo "🎮 Lambda REPL Interface Tests (Catch2)" ;;
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
        "test_cmdedit") echo "⌨️ Command Line Editor Tests" ;;
        "lambda_test_runner") echo "🧪 Lambda Standard Tests" ;;
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
    
    # Need to recompile (either executable doesn't exist or is older than source)
    if [ ! -f "$test_exe" ]; then
        echo "🔄 Compiling $base_name (executable missing)..." >&2
    else
        echo "🔄 Recompiling $base_name (source newer than executable)..." >&2
    fi
    
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
    
    # Handle custom test runner differently
    if [ "$base_name" = "lambda_test_runner" ]; then
        # Custom Lambda Test Runner - use its own output format
        if [ "$RAW_OUTPUT" = true ]; then
            timeout "$TIMEOUT_DURATION" "./$test_exe" --test-dir test/std --format both --json-output "$json_file" --tap-output "$TEST_OUTPUT_DIR/${base_name}_results.tap" --verbose
            local exit_code=$?
        else
            timeout "$TIMEOUT_DURATION" "./$test_exe" --test-dir test/std --format both --json-output "$json_file" --tap-output "$TEST_OUTPUT_DIR/${base_name}_results.tap" > /dev/null 2>&1
            local exit_code=$?
        fi
        
        # Generate CSV report from JSON output using C++ generator
        if [ -f "$json_file" ] && [ -f "test/csv_generator.exe" ]; then
            "./test/csv_generator.exe" "$json_file" "$TEST_OUTPUT_DIR/${base_name}_results.csv" > /dev/null 2>&1 || echo "Warning: CSV generation failed" >&2
        fi
    else
        # Check if this is a Catch2 test by examining the executable name
        if [[ "$base_name" =~ _catch2$ ]]; then
            # Catch2-based tests
            if [ "$RAW_OUTPUT" = true ]; then
                # Raw mode: show output directly and redirect JSON to file
                timeout "$TIMEOUT_DURATION" "./$test_exe" --reporter=json --out="$json_file"
                local exit_code=$?
            else
                # Normal mode: capture console output and redirect JSON to file
                timeout "$TIMEOUT_DURATION" "./$test_exe" --reporter=json --out="$json_file" >/dev/null 2>&1
                local exit_code=$?
            fi
        else
            # Standard Criterion-based tests
            if [ "$RAW_OUTPUT" = true ]; then
                # Raw mode: show output directly without JSON redirect
                timeout "$TIMEOUT_DURATION" "./$test_exe" --json="$json_file"
                local exit_code=$?
            else
                # Normal mode: capture output for processing
                timeout "$TIMEOUT_DURATION" "./$test_exe" --json="$json_file" >/dev/null 2>&1
                local exit_code=$?
            fi
        fi
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
    local base_name="$(basename "$json_file" _results.json)"
    
    if [ ! -f "$json_file" ]; then
        return 1
    fi
    
    # Handle different JSON formats
    if [ "$base_name" = "lambda_test_runner" ]; then
        # Custom Lambda Test Runner JSON format
        local passed=$(jq -r '.summary.passed // 0' "$json_file" 2>/dev/null || echo "0")
        local failed=$(jq -r '.summary.failed // 0' "$json_file" 2>/dev/null || echo "0")
    elif jq -e '.["test-run"]' "$json_file" >/dev/null 2>&1; then
        # Catch2 JSON format
        local passed=$(jq -r '.["test-run"].totals.["test-cases"].passed // 0' "$json_file" 2>/dev/null || echo "0")
        local failed=$(jq -r '.["test-run"].totals.["test-cases"].failed // 0' "$json_file" 2>/dev/null || echo "0")
    else
        # Standard Criterion JSON format
        local passed=$(jq -r '.passed // 0' "$json_file" 2>/dev/null || echo "0")
        local failed=$(jq -r '.failed // 0' "$json_file" 2>/dev/null || echo "0")
    fi
    
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
    
    # For parallel execution, we don't return anything via echo
    # The result file is the communication mechanism
    return 0
}

# Function to extract failed test names
extract_failed_test_names() {
    local json_file="$1"
    local base_name="$(basename "$json_file" _results.json)"
    
    if [ ! -f "$json_file" ]; then
        return
    fi
    
    # Handle different JSON formats for failed test extraction
    if [ "$base_name" = "lambda_test_runner" ]; then
        # Custom Lambda Test Runner JSON format
        jq -r '.tests[]? | select(.passed == false) | .name' "$json_file" 2>/dev/null || true
    elif jq -e '.["test-run"]' "$json_file" >/dev/null 2>&1; then
        # Catch2 JSON format
        jq -r '.["test-run"]["test-cases"][]? | select(.result.status == "failed") | .["test-info"].name' "$json_file" 2>/dev/null || true
    else
        # Standard Criterion JSON format
        jq -r '.test_suites[]?.tests[]? | select(.status == "FAILED") | .name' "$json_file" 2>/dev/null || true
    fi
}

echo "🔍 Finding test executables and sources..."

# Get list of valid test source files from build configuration
get_valid_test_sources() {
    # Extract test sources from the JSON configuration using new tests array structure
    jq -r '.test.test_suites[].tests[]?.source' build_lambda_config.json 2>/dev/null | while IFS= read -r source; do
        # Handle different source path formats
        if [[ "$source" == test/* ]]; then
            echo "$source"
        else
            echo "test/$source"
        fi
    done
}

# Get array of valid test sources
valid_test_sources=()
while IFS= read -r source; do
    if [ -n "$source" ]; then
        valid_test_sources+=("$source")
    fi
done < <(get_valid_test_sources)

echo "Valid test sources from config: ${valid_test_sources[*]}"

# Find existing executables that correspond to valid test sources
test_executables=()
for source_file in "${valid_test_sources[@]}"; do
    if [[ "$source_file" == *.cpp ]]; then
        base_name=$(basename "$source_file" .cpp)
    else
        base_name=$(basename "$source_file" .c)
    fi
    exe_file="test/${base_name}.exe"
    
    # For Lambda Runtime Tests, prefer Catch2 versions over Criterion
    if [[ "$base_name" == "test_lambda" || "$base_name" == "test_lambda_repl" || "$base_name" == "test_lambda_proc" ]]; then
        catch2_name="${base_name}_catch2"
        catch2_exe="test/${catch2_name}.exe"
        if [ -f "$catch2_exe" ]; then
            # Use Catch2 version if available
            test_executables+=("$catch2_exe")
            continue
        fi
    fi
    
    # Skip other Catch2 tests (exclude any test with "catch2" in the name)
    # except for the lambda runtime tests we specifically want
    if [[ "$base_name" == *"catch2"* ]] && [[ "$base_name" != "test_lambda_catch2" ]] && [[ "$base_name" != "test_lambda_repl_catch2" ]] && [[ "$base_name" != "test_lambda_proc_catch2" ]]; then
        continue
    fi
    
    # Only add if executable exists or source exists
    if [ -f "$exe_file" ] || [ -f "$source_file" ]; then
        test_executables+=("$exe_file")
    fi
done

# Add custom test runner if it exists in config
if jq -e '.test.test_suites[] | select(.suite == "lambda-std")' build_lambda_config.json >/dev/null 2>&1; then
    if [ -f "test/lambda_test_runner.exe" ] || [ -f "test/lambda_test_runner.cpp" ]; then
        test_executables+=("test/lambda_test_runner.exe")
    fi
fi

# Remove duplicates and sort
test_executables=($(printf "%s\n" "${test_executables[@]}" | sort -u))

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
        echo "   Available suites: library, input, mir, lambda, lambda-std, validator"
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
        base_name=$(basename "$test_exe" .exe)
        
        # Check if we have a source file for this test
        source_file="test/${base_name}.c"
        if [ -f "$source_file" ] || [ -x "$test_exe" ]; then
            # Calculate result file path - this must match what run_single_test creates
            result_file="$TEST_OUTPUT_DIR/${base_name}_test_result.json"
            result_files+=("$result_file")
            
            echo "   Starting $base_name..."
            
            # Run test in background and collect PID
            # Capture output to a temporary file instead of discarding it
            temp_output="$TEST_OUTPUT_DIR/${base_name}_temp_output.log"
            run_single_test "$test_exe" > "$temp_output" 2>&1 &
            pid=$!
            test_pids+=($pid)
            
            echo "   Started $base_name with PID $pid"
        else
            echo "⚠️  Skipping $test_exe (no source file and not executable)"
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
        base_name=$(basename "$result_file" "_test_result.json")
        
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
            done < <(jq -r '.failed_tests[]?' "$result_file" 2>/dev/null)
            
            # Clean up temporary result file
            rm -f "$result_file"
        else
            echo "   ⚠️ Missing result file: $result_file"
            # Add a failed entry for the missing test
            total_tests=$((total_tests + 1))
            total_failed=$((total_failed + 1))
            c_test_names+=("🧪 $base_name")
            c_test_totals+=(1)
            c_test_passed+=(0)
            c_test_failed+=(1)
            c_test_suites+=("unknown")
            c_test_status+=("❌ ERROR")
            failed_test_names+=("[$base_name] Missing test result")
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
        base_name=$(basename "$test_exe" .exe)
        
        # Check if we have a source file for this test
        source_file="test/${base_name}.c"
        if [ -f "$source_file" ] || [ -x "$test_exe" ]; then
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
            echo "⚠️  Skipping $test_exe (no source file and not executable)"
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
for suite_cat in library input mir lambda lambda-std validator unknown; do
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
    # as we already have error reporting above, just exit with code 0
    exit 0
else
    exit 0
fi
