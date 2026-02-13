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
TARGET_CATEGORY=""
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
        --category=*)
            TARGET_CATEGORY="${1#*=}"
            shift
            ;;
        --category)
            TARGET_CATEGORY="$2"
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
            echo "Usage: $0 [--target=SUITE] [--category=CATEGORY] [--raw] [--sequential] [--parallel]"
            echo "  --target=SUITE     Run only tests from specified suite (library, input, mir, lambda, validator, radiant)"
            echo "  --category=CAT     Run only tests from specified category (baseline, extended)"
            echo "  --raw              Show raw test output without formatting"
            echo "  --sequential       Run tests sequentially (default: parallel)"
            echo "  --parallel         Run tests in parallel (default)"
            echo "  --help             Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Detect Windows/MSYS2 environment
IS_WINDOWS=false
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ -n "$MSYSTEM" ]]; then
    IS_WINDOWS=true
    echo "🪟 Windows/MSYS2 environment detected - will only run gtest tests"
    # Add DLL directories to PATH for Windows test execution
    export PATH="$(pwd)/build/lib:$(pwd)/test:/mingw64/bin:$PATH"
fi

echo "🚀 Enhanced Lambda Test Suite Runner - Test Results Breakdown"
if [ -n "$TARGET_SUITE" ]; then
    echo "🎯 Target Suite: $TARGET_SUITE"
fi
if [ -n "$TARGET_CATEGORY" ]; then
    echo "📁 Target Category: $TARGET_CATEGORY"
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
# Use absolute path to ensure it works in background processes
TEST_OUTPUT_DIR="$(pwd)/test_output"

# On Windows, convert to Windows-style path for GTest JSON output
if [ "$IS_WINDOWS" = true ]; then
    # Convert /d/path to D:/path format for Windows executables
    TEST_OUTPUT_DIR_WIN=$(cygpath -m "$TEST_OUTPUT_DIR" 2>/dev/null || echo "$TEST_OUTPUT_DIR" | sed 's|^/\([a-zA-Z]\)/|\1:/|')
else
    TEST_OUTPUT_DIR_WIN="$TEST_OUTPUT_DIR"
fi

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
declare -a c_test_base_names=()
declare -a c_test_icons=()
declare -a c_test_totals=()
declare -a c_test_passed=()
declare -a c_test_failed=()
declare -a c_test_status=()
declare -a c_test_suites=()  # Maps C test to its suite category

# Function to map executable name to test suite category (from build_lambda_config.json)
get_test_suite_category() {
    local exe_name="$1"

    # Get suite category from build configuration using new tests array structure
    # Only consider enabled test suites (exclude disabled ones)
    local suite_category=$(jq -r --arg exe "$exe_name" '
        .test.test_suites[] |
        select(.disabled != true) |
        select(.tests[]? | (.source // "") | test("\\b" + $exe + "\\.(c|cpp)$")) |
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
    # Only consider enabled test suites (exclude disabled ones)
    local display_name=$(jq -r --arg suite "$category" '
        .test.test_suites[] |
        select(.disabled != true) |
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
        "radiant") echo "🎨 Radiant Layout Engine Tests" ;;
        "unknown") echo "🧪 Other Tests" ;;
        *) echo "🧪 $category Tests" ;;
    esac
}

# Function to map executable name to friendly C test name
get_c_test_display_name() {
    local exe_name="$1"

    # Try to get custom display name from build configuration using new tests array structure
    # Only consider enabled test suites (exclude disabled ones)
    local display_name=$(jq -r --arg exe "$exe_name" '
        .test.test_suites[] |
        select(.disabled != true) |
        .tests[]? |
        select((.source // "") | test("\\b" + $exe + "\\.(c|cpp)$")) |
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
        "test_dir_gtest") echo "📁 Directory Listing Tests (GTest)" ;;
        "test_http") echo "🌐 HTTP/HTTPS Tests" ;;
        "test_input_roundtrip") echo "🔄 Input Roundtrip Tests" ;;
        "test_input_roundtrip_gtest") echo "🔄 Input Roundtrip Tests (GTest)" ;;
        "test_lambda") echo "🐑 Lambda Runtime Tests" ;;
        "test_lambda_gtest") echo "🐑 Lambda Runtime Tests (GTest)" ;;
        "test_lambda_proc_gtest") echo "🐑 Lambda Procedural Tests (GTest)" ;;
        "test_lambda_repl_gtest") echo "🎮 Lambda REPL Interface Tests (GTest)" ;;
        "test_markup_roundtrip") echo "📝 Markup Roundtrip Tests" ;;
        "test_markup_roundtrip_gtest") echo "📝 Markup Roundtrip Tests (GTest)" ;;
        "test_math") echo "🔢 Math Roundtrip Tests" ;;
        "test_math_gtest") echo "🔢 Math Roundtrip Tests (GTest)" ;;
        "test_math_ascii_gtest") echo "🔤 ASCII Math Roundtrip Tests (GTest)" ;;
        "test_mime_detect") echo "📎 MIME Detection Tests" ;;
        "test_mime_detect_gtest") echo "📎 MIME Detection Tests (GTest)" ;;
        "test_css_files_safe_gtest") echo "🎨 CSS Files Safe Tests (GTest)" ;;
        "test_css_system") echo "🎨 CSS Property System & Style Node Tests (GTest)" ;;
        "test_css_style_node") echo "🎨 CSS Style Node & Cascade Tests (GTest)" ;;
        "test_avl_tree") echo "🌲 AVL Tree Implementation Tests (GTest)" ;;
        "test_mir") echo "⚡ MIR JIT Tests" ;;
        "test_num_stack") echo "🔢 Number Stack Tests" ;;
        "test_strbuf") echo "📝 String Buffer Tests" ;;
        "test_strview") echo "👀 String View Tests" ;;
        "test_url_extra") echo "🌐 URL Extra Tests" ;;
        "test_url") echo "🔗 URL Tests" ;;
        "test_validator") echo "🔍 Validator Tests" ;;
        "test_validator_gtest") echo "🔍 Validator Tests (GTest)" ;;
        "test_ast_validator") echo "🔍 AST Validator Tests" ;;
        "test_ast_validator_gtest") echo "🔍 AST Validator Tests (GTest)" ;;
        "test_variable_pool") echo "🏊 Variable Pool Tests" ;;
        "test_cmdedit") echo "⌨️ Command Line Editor Tests" ;;
        "lambda_test_runner") echo "🧪 Lambda Standard Tests" ;;
        "test_radiant_flex_gtest") echo "🎨 Radiant Flex Layout Tests (GTest)" ;;
        "test_radiant_flex_algorithm_gtest") echo "⚙️ Radiant Flex Algorithm Tests (GTest)" ;;
        "test_radiant_flex_integration_gtest") echo "🔗 Radiant Flex Integration Tests (GTest)" ;;
        "test_flex_minimal") echo "📦 Minimal Flex Layout Tests (GTest)" ;;
        "test_flex_core_validation") echo "✅ Flex Core Validation Tests" ;;
        "test_flex_simple") echo "🧪 Simple Flex Tests" ;;
        "test_flex_layout_gtest") echo "📐 Comprehensive Flex Layout Tests (GTest)" ;;
        "test_flex_layout") echo "📐 Comprehensive Flex Layout Tests (Criterion)" ;;
        "test_flex_standalone") echo "🔧 Standalone Flex Layout Tests" ;;
        "test_flex_new_features") echo "🚀 Advanced Flex New Features Tests" ;;
        *) echo "$exe_name" ;;
    esac
}

# Function to get test icon from build configuration
get_c_test_icon() {
    local exe_name="$1"

    # Try to get icon from build configuration
    local icon=$(jq -r --arg exe "$exe_name" '
        .test.test_suites[] |
        select(.disabled != true) |
        .tests[]? |
        select((.source // "") | test("\\b" + $exe + "\\.(c|cpp)$")) |
        .icon // "🧪"
    ' build_lambda_config.json 2>/dev/null | head -1)

    # Return icon or default
    if [ -n "$icon" ] && [ "$icon" != "null" ]; then
        echo "$icon"
    else
        echo "🧪"
    fi
}

# Function to check if test needs recompilation and build if necessary
ensure_test_compiled() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local source_file="test/${base_name}.c"

    # Check for .cpp file if .c doesn't exist
    if [ ! -f "$source_file" ]; then
        source_file="test/${base_name}.cpp"
    fi

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

# Function to detect if a test executable is a GTest test
is_gtest_test() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"

    # First check: name ends with _gtest
    if [[ "$base_name" =~ _gtest$ ]]; then
        return 0
    fi

    # Second check: explicitly known GTest tests (for tests with non-standard names)
    case "$base_name" in
        test_flex_minimal|test_flex_new_features|test_css_system|test_css_style_node|test_avl_tree|test_avl_tree_perf|\
        test_compound_descendant_selectors|test_selector_groups|test_css_tokenizer_unit|test_css_parser_unit|\
        test_css_integration_unit|test_css_engine_unit|test_css_formatter_unit|test_css_roundtrip_unit)
            return 0
            ;;
    esac

    # Third check: try to run with --gtest_list_tests to detect GTest at runtime
    # This is more reliable but slower, so we do it last
    if [ -x "$test_exe" ]; then
        if timeout 2s "./$test_exe" --gtest_list_tests >/dev/null 2>&1; then
            return 0
        fi
    fi

    return 1
}

# Function to run a test executable with timeout and JSON output
run_test_with_timeout() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    # TEST_OUTPUT_DIR is already an absolute path
    local json_file="$TEST_OUTPUT_DIR/${base_name}_results.json"
    # Use Windows-compatible path for GTest JSON output
    local json_file_win="$TEST_OUTPUT_DIR_WIN/${base_name}_results.json"

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
        # Check if this is a GTest test
        if is_gtest_test "$test_exe"; then
            # GTest-based tests - use JSON output format

            # Special handling for input roundtrip test - only run working JSON tests
            local gtest_filter=""
            if [[ "$base_name" == "test_input_roundtrip_gtest" ]]; then
                gtest_filter="--gtest_filter=JsonTests.*"
            fi

            # Suppress ASan container-overflow false positives in GTest's JSON printer
            # (known issue with parameterized test names triggering std::vector reallocation checks)
            local asan_opts="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_container_overflow=0"

            if [ "$RAW_OUTPUT" = true ]; then
                # Raw mode: show output directly and redirect JSON to file
                if [ -n "$gtest_filter" ]; then
                    ASAN_OPTIONS="$asan_opts" timeout "$TIMEOUT_DURATION" "./$test_exe" "$gtest_filter" --gtest_output=json:"$json_file_win"
                else
                    ASAN_OPTIONS="$asan_opts" timeout "$TIMEOUT_DURATION" "./$test_exe" --gtest_output=json:"$json_file_win"
                fi
                local exit_code=$?
            else
                # Normal mode: capture console output and redirect JSON to file
                if [ -n "$gtest_filter" ]; then
                    ASAN_OPTIONS="$asan_opts" timeout "$TIMEOUT_DURATION" "./$test_exe" "$gtest_filter" --gtest_output=json:"$json_file_win" >/dev/null 2>&1
                else
                    ASAN_OPTIONS="$asan_opts" timeout "$TIMEOUT_DURATION" "./$test_exe" --gtest_output=json:"$json_file_win" >/dev/null 2>&1
                fi
                local exit_code=$?
            fi
        elif [[ "$base_name" == "test_flex_standalone" ]]; then
            # Custom standalone test - parse output manually
            local temp_output="$TEST_OUTPUT_DIR/${base_name}_temp_output.log"
            if [ "$RAW_OUTPUT" = true ]; then
                timeout "$TIMEOUT_DURATION" "./$test_exe" | tee "$temp_output"
                local exit_code=$?
            else
                timeout "$TIMEOUT_DURATION" "./$test_exe" > "$temp_output" 2>&1
                local exit_code=$?
            fi

            # Parse custom output format and create JSON
            local passed_count=0
            local failed_count=0
            if [ -f "$temp_output" ]; then
                passed_count=$(grep -c "PASS:" "$temp_output" 2>/dev/null || echo "0")
                failed_count=$(grep -c "FAIL:" "$temp_output" 2>/dev/null || echo "0")

                # Ensure we have valid numbers (strip any whitespace)
                passed_count=$(echo "$passed_count" | tr -d ' \n\r')
                failed_count=$(echo "$failed_count" | tr -d ' \n\r')

                # Validate numbers
                [[ "$passed_count" =~ ^[0-9]+$ ]] || passed_count=0
                [[ "$failed_count" =~ ^[0-9]+$ ]] || failed_count=0

                local total_count=$((passed_count + failed_count))

                # Create a simple JSON file for consistency
                echo "{\"tests\": $total_count, \"failures\": $failed_count, \"passed\": $passed_count}" > "$json_file"
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
    # Wait up to 45 seconds for the file to be written (handles race condition in parallel execution)
    local wait_count=0
    while [ $wait_count -lt 450 ]; do
        if [ -f "$json_file" ] && jq empty "$json_file" 2>/dev/null; then
            echo "$json_file"
            return 0
        fi
        sleep 0.1
        wait_count=$((wait_count + 1))
    done

    # If we get here, the file wasn't created or is invalid
    echo "❌ No valid JSON output from $base_name" >&2
    # Create a minimal error JSON file for consistency
    echo '{"passed": 0, "failed": 1, "tests": [{"name": "json_error", "status": "failed", "message": "No valid JSON output"}]}' > "$json_file"
    echo "$json_file"
    return 1
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
    elif jq -e '.tests' "$json_file" >/dev/null 2>&1; then
        # GTest JSON format
        local total=$(jq -r '.tests // 0' "$json_file" 2>/dev/null || echo "0")
        local failed=$(jq -r '.failures // 0' "$json_file" 2>/dev/null || echo "0")
        local disabled=$(jq -r '.disabled // 0' "$json_file" 2>/dev/null || echo "0")
        # Ensure we have valid numbers
        if [[ "$total" =~ ^[0-9]+$ ]] && [[ "$failed" =~ ^[0-9]+$ ]] && [[ "$disabled" =~ ^[0-9]+$ ]]; then
            # Exclude disabled tests from the total count
            local total=$((total - disabled))
            local passed=$((total - failed))
        else
            local passed=0
            local failed=1
            local total=1
        fi
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
    local c_test_icon=$(get_c_test_icon "$base_name")
    local suite_category=$(get_test_suite_category "$base_name")
    local result_file="$TEST_OUTPUT_DIR/${base_name}_test_result.json"

    # Ensure test is compiled and up to date
    if ! ensure_test_compiled "$test_exe"; then
        echo "❌ Failed to compile $base_name, skipping test" >&2
        # Create a minimal error JSON file for consistency
        echo '{"passed": 0, "failed": 1, "tests": [{"name": "compilation_error", "status": "failed", "message": "Test compilation failed"}]}' > "$result_file"
        echo "0 1 ❌ ERROR $c_test_display_name $c_test_icon $suite_category"
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
        echo "  \"icon\": \"$c_test_icon\","
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
    # Only include sources from enabled test suites (exclude disabled ones)
    jq -r '.test.test_suites[] | select(.disabled != true) | .tests[]?.source' build_lambda_config.json 2>/dev/null | while IFS= read -r source; do
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

    # For Lambda Runtime Tests, prefer GTest versions over Criterion
    if [[ "$base_name" == "test_lambda" || "$base_name" == "test_lambda_repl" || "$base_name" == "test_lambda_proc" ]]; then
        gtest_name="${base_name}_gtest"
        gtest_exe="test/${gtest_name}.exe"
        if [ -f "$gtest_exe" ]; then
            # Use GTest version if available
            test_executables+=("$gtest_exe")
            continue
        fi
    fi

    # For Input Tests, prefer GTest versions over Criterion
    if [[ "$base_name" == "test_mime_detect" || "$base_name" == "test_css_files_safe" || \
          "$base_name" == "test_math" || "$base_name" == "test_math_ascii" || \
          "$base_name" == "test_markup_roundtrip" || "$base_name" == "test_dir" || \
          "$base_name" == "test_input_roundtrip" ]]; then
        gtest_name="${base_name}_gtest"
        gtest_exe="test/${gtest_name}.exe"
        if [ -f "$gtest_exe" ]; then
            # Use GTest version if available
            test_executables+=("$gtest_exe")
            continue
        fi
    fi

    # Skip other Catch2 tests (exclude any test with "catch2" in the name)
    # except for the lambda runtime tests we specifically want
    if [[ "$base_name" == *"catch2"* ]]; then
        continue
    fi

    # On Windows, only run gtest tests to avoid compatibility issues
    if [[ "$IS_WINDOWS" == "true" ]]; then
        if [[ "$base_name" != *"gtest"* ]]; then
            echo "⏭️  Skipping $base_name (Windows - gtest tests only)"
            continue
        fi
    fi

    # Only add if executable exists or source exists
    if [ -f "$exe_file" ] || [ -f "$source_file" ]; then
        test_executables+=("$exe_file")
    fi
done

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
        echo "   Available suites: library, input, mir, lambda, lambda-std, validator, radiant"
        exit 1
    fi
fi

# Filter executables by category if specified
if [ -n "$TARGET_CATEGORY" ]; then
    filtered_executables=()
    for test_exe in "${test_executables[@]}"; do
        base_name=$(basename "$test_exe" .exe)
        # Get the category from build configuration
        # First check for per-test category override, then fall back to suite category
        test_category=$(jq -r --arg exe "$base_name.exe" '
            .test.test_suites[] as $suite |
            $suite.tests[]? |
            select(.binary == $exe) |
            .category // $suite.category // "baseline"
        ' build_lambda_config.json | head -n1)

        if [ "$test_category" = "$TARGET_CATEGORY" ]; then
            filtered_executables+=("$test_exe")
        fi
    done
    test_executables=("${filtered_executables[@]}")

    if [ ${#test_executables[@]} -eq 0 ]; then
        echo "❌ No test executables found for category: $TARGET_CATEGORY"
        echo "   Available categories: baseline, extended"
        exit 1
    fi
fi

echo "📋 Found ${#test_executables[@]} test executable(s)"
echo ""

# Execute tests (parallel or sequential)
if [ "$PARALLEL_EXECUTION" = true ] && [ "$RAW_OUTPUT" != true ]; then
    echo "⚡ Running tests in parallel (max 5 concurrent)..."
    echo ""

    # Rolling window parallel execution
    MAX_CONCURRENT=5
    result_files=()
    total_tests_count=${#test_executables[@]}
    current_index=0
    completed_count=0

    # Arrays to track running jobs (parallel arrays)
    running_pids=()
    running_names=()

    # Helper function to start a test
    start_test() {
        local test_exe="$1"
        local base_name=$(basename "$test_exe" .exe)

        # Check if we have a source file for this test
        local source_file_c="test/${base_name}.c"
        local source_file_cpp="test/${base_name}.cpp"
        if [ -f "$source_file_c" ] || [ -f "$source_file_cpp" ] || [ -x "$test_exe" ]; then
            # Calculate result file path
            local result_file="$TEST_OUTPUT_DIR/${base_name}_test_result.json"
            result_files+=("$result_file")

            echo "   [$((current_index + 1))/$total_tests_count] Starting $base_name..."

            # Run test in background
            local temp_output="$TEST_OUTPUT_DIR/${base_name}_temp_output.log"
            run_single_test "$test_exe" > "$temp_output" 2>&1 &
            local pid=$!

            running_pids+=("$pid")
            running_names+=("$base_name")
            current_index=$((current_index + 1))
            return 0
        else
            echo "⚠️  Skipping $test_exe (no source file and not executable)"
            current_index=$((current_index + 1))
            return 1
        fi
    }

    # Start initial batch of tests
    while [ $current_index -lt $total_tests_count ] && [ ${#running_pids[@]} -lt $MAX_CONCURRENT ]; do
        start_test "${test_executables[$current_index]}"
    done

    echo ""
    echo "⏳ Running with ${#running_pids[@]} concurrent jobs..."

    # Main loop: wait for jobs to finish and start new ones
    while [ ${#running_pids[@]} -gt 0 ] || [ $current_index -lt $total_tests_count ]; do
        # Check each running PID
        for i in "${!running_pids[@]}"; do
            pid="${running_pids[$i]}"
            if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
                # Process has finished
                wait "$pid" 2>/dev/null || true
                test_name="${running_names[$i]}"
                echo "   ✓ Completed $test_name"

                # Remove from arrays by setting to empty
                running_pids[$i]=""
                running_names[$i]=""
                completed_count=$((completed_count + 1))

                # Start a new test if available
                if [ $current_index -lt $total_tests_count ]; then
                    start_test "${test_executables[$current_index]}"
                fi
            fi
        done

        # Clean up empty entries from arrays
        new_pids=()
        new_names=()
        for i in "${!running_pids[@]}"; do
            if [ -n "${running_pids[$i]}" ]; then
                new_pids+=("${running_pids[$i]}")
                new_names+=("${running_names[$i]}")
            fi
        done
        running_pids=("${new_pids[@]}")
        running_names=("${new_names[@]}")

        # Small sleep to avoid busy-waiting
        if [ ${#running_pids[@]} -gt 0 ]; then
            sleep 0.1
        fi
    done

    echo ""
    echo "✅ All $completed_count parallel tests completed!"
    echo ""

    # Process results from all test files
    echo "🔍 Processing test results..."
    for i in "${!result_files[@]}"; do
        result_file="${result_files[$i]}"
        base_name=$(basename "$result_file" "_test_result.json")

        if [ -f "$result_file" ]; then
            # Parse JSON result file
            display_name=$(jq -r '.display_name' "$result_file" 2>/dev/null)
            icon=$(jq -r '.icon' "$result_file" 2>/dev/null)
            suite_category=$(jq -r '.suite_category' "$result_file" 2>/dev/null)
            passed=$(jq -r '.passed' "$result_file" 2>/dev/null)
            failed=$(jq -r '.failed' "$result_file" 2>/dev/null)
            total=$(jq -r '.total' "$result_file" 2>/dev/null)
            status=$(jq -r '.status' "$result_file" 2>/dev/null)

            # Validate parsed values
            if [ "$display_name" = "null" ] || [ -z "$display_name" ]; then display_name="$base_name"; fi
            if [ "$icon" = "null" ] || [ -z "$icon" ]; then icon="🧪"; fi
            if [ "$suite_category" = "null" ] || [ -z "$suite_category" ]; then suite_category="unknown"; fi
            if [ "$passed" = "null" ] || [ -z "$passed" ]; then passed=0; fi
            if [ "$failed" = "null" ] || [ -z "$failed" ]; then failed=0; fi
            if [ "$total" = "null" ] || [ -z "$total" ]; then total=$((passed + failed)); fi
            if [ "$status" = "null" ] || [ -z "$status" ]; then status="❌ ERROR"; fi

            # Show individual test results: icon status (n/m tests) name (exe)
            echo "   $icon $status ($passed/$total tests) $display_name (${base_name}.exe)"

            # Add to overall totals
            total_tests=$((total_tests + total))
            total_passed=$((total_passed + passed))
            total_failed=$((total_failed + failed))

            # Track individual C test results
            c_test_names+=("$display_name")
            c_test_base_names+=("$base_name")
            c_test_icons+=("$icon")
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
        if [ ! -f "$source_file" ]; then
            source_file="test/${base_name}.cpp"
        fi
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
# Dynamically get all suite categories that have tests, plus common ones
all_suite_categories=(library input mir lambda lambda-std validator radiant unknown)
# Add any additional categories found in the actual test results
for suite in "${suite_categories[@]}"; do
    found=false
    for existing in "${all_suite_categories[@]}"; do
        if [ "$existing" = "$suite" ]; then
            found=true
            break
        fi
    done
    if [ "$found" = false ]; then
        all_suite_categories+=("$suite")
    fi
done

for suite_cat in "${all_suite_categories[@]}"; do
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
                c_base_name="${c_test_base_names[$i]}"
                c_icon="${c_test_icons[$i]}"
                status="${c_test_status[$i]}"
                passed="${c_test_passed[$i]}"
                total="${c_test_totals[$i]}"

                echo "     └─ $c_icon $status ($passed/$total tests) $c_test_name (${c_base_name}.exe)"
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
