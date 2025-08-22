#!/bin/bash

# Enhanced Lambda Test Suite Runner - Two-Level Breakdown
# Groups C-level tests by their test suite categories from build_lambda_config.json

set -e

echo "🚀 Enhanced Lambda Test Suite Runner - Two-Level Breakdown"
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

# Function to run a test executable with timeout and JSON output
run_test_with_timeout() {
    local test_exe="$1"
    local base_name="$(basename "$test_exe" .exe)"
    local json_file="$TEST_OUTPUT_DIR/${base_name}_results.json"
    
    echo "📋 Running $base_name..." >&2
    
    # Run test with timeout
    if timeout "$TIMEOUT_DURATION" "./$test_exe" --json="$json_file" >/dev/null 2>&1; then
        echo "✅ $base_name completed successfully" >&2
    else
        local exit_code=$?
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
    else
        echo "❌ No valid JSON output from $base_name" >&2
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

# Function to extract failed test names
extract_failed_test_names() {
    local json_file="$1"
    
    if [ ! -f "$json_file" ]; then
        return
    fi
    
    # Extract failed test names using jq - handle Criterion JSON structure
    jq -r '.test_suites[]?.tests[]? | select(.status == "FAILED") | .name' "$json_file" 2>/dev/null || true
}

echo "🔍 Finding test executables..."
test_executables=($(find test -name "test_*.exe" -type f 2>/dev/null | sort))

if [ ${#test_executables[@]} -eq 0 ]; then
    echo "❌ No test executables found in test/ directory"
    echo "   Make sure you've run ./test/test_all.sh first to build the tests"
    exit 1
fi

echo "📋 Found ${#test_executables[@]} test executable(s)"
echo ""

# Run each test executable and group by suite
for test_exe in "${test_executables[@]}"; do
    if [ -x "$test_exe" ]; then
        base_name=$(basename "$test_exe" .exe)
        c_test_display_name=$(get_c_test_display_name "$base_name")
        suite_category=$(get_test_suite_category "$base_name")
        
        echo "🏃 Running $c_test_display_name..."
        
        json_file=$(run_test_with_timeout "$test_exe")
        
        if [ $? -eq 0 ] && [ -n "$json_file" ]; then
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
                c_test_status+=("⚠️  NO OUTPUT")
                echo "   ⚠️  No valid test results"
            fi
        else
            # Test execution failed
            c_test_names+=("$c_test_display_name")
            c_test_totals+=(0)
            c_test_passed+=(0)
            c_test_failed+=(0)
            c_test_suites+=("$suite_category")
            c_test_status+=("❌ ERROR")
            echo "   ❌ Test execution failed"
        fi
    else
        echo "⚠️  Skipping non-executable: $test_exe"
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

echo ""
echo "=============================================================="
echo "🏁 TWO-LEVEL TEST RESULTS BREAKDOWN"
echo "=============================================================="

# Level 1: Test suite categories (from build_lambda_config.json)
echo "📊 Level 1 - Test Suite Categories:"
for i in "${!suite_category_names[@]}"; do
    suite_name="${suite_category_names[$i]}"
    status="${suite_category_status[$i]}"
    passed="${suite_category_passed[$i]}"
    total="${suite_category_totals[$i]}"
    
    echo "   $suite_name $status ($passed/$total tests)"
done

echo ""
echo "📊 Level 2 - Individual C Tests by Suite:"

# Level 2: Individual C tests grouped by suite
for suite_cat in library input mir lambda validator unknown; do
    suite_display=$(get_suite_category_display_name "$suite_cat")
    suite_has_tests=false
    
    for i in "${!c_test_suites[@]}"; do
        if [ "${c_test_suites[$i]}" = "$suite_cat" ]; then
            if [ "$suite_has_tests" = false ]; then
                echo "   $suite_display:"
                suite_has_tests=true
            fi
            
            c_test_name="${c_test_names[$i]}"
            status="${c_test_status[$i]}"
            passed="${c_test_passed[$i]}"
            total="${c_test_totals[$i]}"
            
            echo "     └─ $c_test_name $status ($passed/$total tests)"
        fi
    done
done

echo ""
echo "📊 Overall Results:"
echo "   Total Tests: $total_tests"
echo "   ✅ Passed:   $total_passed"
echo "   ❌ Failed:   $total_failed"

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
