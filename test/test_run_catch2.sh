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

# Function to map executable name to friendly Catch2 test name (removing "(Catch2)" suffix to match old system)
get_c_test_display_name() {
    local exe_name="$1"
    
    # Dynamic display names based on test name patterns
    case "$exe_name" in
        "test_css_files_safe_catch2") echo "🎨 CSS Files Safe Tests" ;;
        "test_css_frameworks_catch2") echo "🎨 CSS Framework Tests" ;;
        "test_css_integration_catch2") echo "🎨 CSS Integration Tests" ;;
        "test_css_parser_catch2") echo "🎨 CSS Parser Tests" ;;
        "test_css_tokenizer_catch2") echo "🎨 CSS Tokenizer Tests" ;;
        "test_datetime_catch2") echo "📅 DateTime Tests" ;;
        "test_dir_catch2") echo "📁 Directory Listing Tests" ;;
        "test_http_catch2") echo "🌐 HTTP/HTTPS Tests" ;;
        "test_jsx_roundtrip_catch2") echo "⚛️ JSX Roundtrip Tests" ;;
        "test_lambda_catch2") echo "🐑 Lambda Runtime Tests" ;;
        "test_lambda_proc_catch2") echo "🐑 Lambda Procedural Tests" ;;
        "test_lambda_repl_catch2") echo "🎮 Lambda REPL Interface Tests" ;;
        "test_markup_roundtrip_catch2") echo "📝 Markup Roundtrip Tests" ;;
        "test_math_ascii_catch2") echo "🔤 ASCII Math Roundtrip Tests" ;;
        "test_math_catch2") echo "🔢 Math Roundtrip Tests" ;;
        "test_mdx_roundtrip_catch2") echo "📝 MDX Roundtrip Tests" ;;
        "test_mime_detect_catch2") echo "📎 MIME Detection Tests" ;;
        "test_num_stack_catch2") echo "🔢 Number Stack Tests" ;;
        "test_strbuf_catch2") echo "📝 String Buffer Tests" ;;
        "test_stringbuf_catch2") echo "🧵 StringBuf Tests" ;;
        "test_strview_catch2") echo "👀 String View Tests" ;;
        "test_sysinfo_catch2") echo "🖥️ System Information Tests" ;;
        "test_url_catch2") echo "🌐 URL Parser Tests" ;;
        "test_url_extra_catch2") echo "🌐 URL Parser Extended Tests" ;;
        "test_validator_catch2") echo "🔍 Validator Tests" ;;
        "test_variable_pool_catch2") echo "🏊 Variable Pool Tests" ;;
        *) echo "🧪 $exe_name" ;;
    esac
}

# Function to run a test executable with timeout and parse Catch2 output
run_test_with_timeout() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local output_file="$TEST_OUTPUT_DIR/${base_name}_output.txt"
    
    echo "📋 Running $base_name..." >&2
    
    # Run Catch2 test with reporter that gives us parseable output
    if [ "$RAW_OUTPUT" = true ]; then
        timeout "$TIMEOUT_DURATION" "./$test_exe" --reporter compact
        local exit_code=$?
    else
        timeout "$TIMEOUT_DURATION" "./$test_exe" --reporter compact > "$output_file" 2>&1
        local exit_code=$?
    fi
    
    if [ $exit_code -eq 0 ]; then
        echo "✅ $base_name completed successfully" >&2
    else
        if [ $exit_code -eq 124 ]; then
            echo "⏰ $base_name timed out after ${TIMEOUT_DURATION}s" >&2
        else
            echo "⚠️  $base_name completed with exit code $exit_code" >&2
        fi
    fi
    
    echo "$output_file $exit_code"
    return 0
}

# Function to parse Catch2 output and extract counts
parse_catch2_results() {
    local output_file="$1"
    local exit_code="$2"
    
    if [ ! -f "$output_file" ]; then
        echo "0 1"
        return 1
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
            passed=$(grep "test cases:" "$output_file" 2>/dev/null | grep -o "[0-9]* passed" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep "test cases:" | grep -o "[0-9]* passed" | grep -o "[0-9]*" || echo "0")
            failed=$(grep "test cases:" "$output_file" 2>/dev/null | grep -o "[0-9]* failed" | grep -o "[0-9]*" || \
                     strings "$output_file" | grep "test cases:" | grep -o "[0-9]* failed" | grep -o "[0-9]*" || echo "0")
        else
            # Fallback - assume at least one failure
            passed=0
            failed=1
        fi
    fi
    
    echo "$passed $failed"
}

# Function to extract failed test names from Catch2 output
extract_failed_test_names() {
    local output_file="$1"
    local base_name="$2"
    
    if [ ! -f "$output_file" ]; then
        return
    fi
    
    # Extract failed test names from Catch2 output
    # Look for lines starting with test file path and FAILED
    grep "FAILED:" "$output_file" | sed 's/.*FAILED: //' | while IFS= read -r line; do
        if [ -n "$line" ]; then
            echo "[$base_name] $line"
        fi
    done
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

# Execute tests (sequential for now - Catch2 tests are fast)
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
                    echo "   ❌ $failed/$c_test_total tests failed"
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
echo "   - Individual output files: *_output.txt"
echo "   - Two-level summary: test_summary_catch2.json"

# Exit with appropriate code
if [ $total_failed -gt 0 ]; then
    exit 1
else
    exit 0
fi
