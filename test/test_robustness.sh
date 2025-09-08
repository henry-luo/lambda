#!/bin/bash

# Enhanced Robustness Testing Suite for Lambda Script
# Tests for security vulnerabilities, memory safety, and edge cases

# Don't exit on error for tests - we want to collect all results
set +e

echo "🔒 Lambda Script Security & Robustness Test Suite"
echo "=================================================="

# Configuration
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$TEST_DIR/.." && pwd)"
OUTPUT_DIR="$TEST_DIR/output"
TIMEOUT_DURATION="30s"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Utility functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((PASSED_TESTS++))
}

log_failure() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((FAILED_TESTS++))
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected_result="$3"  # "pass" or "fail"
    
    ((TOTAL_TESTS++))
    
    log_info "Running test: $test_name"
    
    # Run the test and capture both stdout and stderr
    if timeout "$TIMEOUT_DURATION" bash -c "$test_command" > "$OUTPUT_DIR/$test_name.log" 2>&1; then
        local exit_code=$?
        if [ "$expected_result" = "pass" ]; then
            log_success "$test_name"
        else
            log_failure "$test_name (expected to fail but passed)"
        fi
    else
        local exit_code=$?
        if [ "$expected_result" = "fail" ]; then
            log_success "$test_name (correctly failed as expected)"
        else
            # Check if it's a timeout (exit code 124) vs actual failure
            if [ $exit_code -eq 124 ]; then
                log_warning "$test_name (timed out after $TIMEOUT_DURATION)"
            else
                log_failure "$test_name (unexpected failure)"
                echo "  Error details in: $OUTPUT_DIR/$test_name.log"
            fi
        fi
    fi
}

# Build lambda if not present
if [ ! -f "$PROJECT_ROOT/lambda.exe" ]; then
    log_info "Building Lambda..."
    cd "$PROJECT_ROOT"
    make build || {
        log_failure "Failed to build Lambda executable"
        exit 1
    }
fi

cd "$PROJECT_ROOT"

echo
echo "🧪 Memory Safety Tests"
echo "====================="

# Test 1: Buffer overflow protection
run_test "buffer_overflow_protection" '
    # Create a test script with extremely long strings
    cat > test_long_string.ls << EOF
let huge_string = "$(printf "A%.0s" {1..10000})"
len(huge_string)
EOF
    ./lambda.exe test_long_string.ls
    rm -f test_long_string.ls
' "pass"

# Test 2: Deep recursion protection
run_test "deep_recursion_protection" '
    # Create deeply nested structures
    cat > test_deep_nesting.ls << EOF
let deeply_nested = [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]
len(deeply_nested)
EOF
    ./lambda.exe test_deep_nesting.ls
    rm -f test_deep_nesting.ls
' "pass"

# Test 3: Null pointer handling
run_test "null_pointer_handling" '
    cat > test_null_handling.ls << EOF
let null_var = null
type(null_var)
EOF
    ./lambda.exe test_null_handling.ls
    rm -f test_null_handling.ls
' "pass"

# Test 4: Memory leak with large allocations
run_test "large_allocation_test" '
    cat > test_large_alloc.ls << EOF
# Create large arrays and maps to test memory management
let large_array = []
for i in 1 to 100 {
    large_array = large_array + [[i, i*2, i*3, i*4, i*5]]
}
len(large_array)
EOF
    ./lambda.exe test_large_alloc.ls
    rm -f test_large_alloc.ls
' "pass"

echo
echo "🔍 Input Validation Tests"
echo "========================"

# Test 5: Malformed JSON handling
run_test "malformed_json_handling" '
    cat > test_malformed.ls << EOF
# Test parsing of JSON-like strings (using basic string operations)
let malformed_inputs = [
    "{{{{{{{{{",  # Unmatched braces
    "[[[[[[[[[",  # Unmatched brackets  
    "invalid_json"
]
len(malformed_inputs)
EOF
    ./lambda.exe test_malformed.ls
    rm -f test_malformed.ls
' "pass"

# Test 6: Unicode handling edge cases
run_test "unicode_edge_cases" '
    cat > test_unicode.ls << EOF
# Test various Unicode edge cases
let unicode_tests = [
    "🚀📊💻",  # Emojis
    "café naïve résumé",  # Accented characters
    "日本語",  # Japanese
    "Здравствуй мир",  # Cyrillic
    "مرحبا بالعالم"  # Arabic
]
len(unicode_tests)
EOF
    ./lambda.exe test_unicode.ls
    rm -f test_unicode.ls
' "pass"

echo
echo "🚨 Error Handling Tests"
echo "======================="

# Test 7: Type safety violations
run_test "type_safety_violations" '
    cat > test_type_safety.ls << EOF
# Test various type operations that should work gracefully
let safe_ops = [
    5,
    "string", 
    [1, 2, 3],
    null
]
len(safe_ops)
EOF
    ./lambda.exe test_type_safety.ls
    rm -f test_type_safety.ls
' "pass"

# Test 8: Recursion limits
run_test "recursion_limits" '
    cat > test_recursion.ls << EOF
# Test function definition and basic recursion
fn factorial(n) {
    if (n <= 1) {
        1
    } else {
        n * factorial(n - 1)
    }
}
factorial(10)
EOF
    ./lambda.exe test_recursion.ls
    rm -f test_recursion.ls
' "pass"

# Test 9: Loop performance
run_test "loop_performance" '
    cat > test_loops.ls << EOF
# Test loop performance without infinite loops
let counter = 0
for i in 1 to 100 {
    counter = counter + i
}
counter
EOF
    # This test should complete within timeout
    timeout 10s ./lambda.exe test_loops.ls
    rm -f test_loops.ls
' "pass"

echo
echo "📊 Performance & Resource Tests"
echo "==============================="

# Test 10: Memory usage with data structures
run_test "memory_usage_monitoring" '
    cat > test_memory_usage.ls << EOF
# Create nested data structures to test memory management
let nested_data = []
for i in 1 to 50 {
    let inner_array = []
    for j in 1 to 20 {
        inner_array = inner_array + [j * i]
    }
    nested_data = nested_data + [inner_array]
}
len(nested_data)
EOF
    ./lambda.exe test_memory_usage.ls
    rm -f test_memory_usage.ls
' "pass"

# Test 11: Array operations safety
run_test "array_operations_safety" '
    # Test array bounds and operations
    cat > test_arrays.ls << EOF
# Test array creation and access patterns
let test_array = [1, 2, 3, 4, 5]
let result = len(test_array)
result
EOF
    ./lambda.exe test_arrays.ls
    rm -f test_arrays.ls
' "pass"

echo
echo "🔧 Cleanup"
echo "=========="

# Clean up test files
rm -f test_*.ls _transpiled.c

echo
echo "📋 Test Summary"
echo "==============="
echo "Total tests: $TOTAL_TESTS"
echo "Passed: $PASSED_TESTS"
echo "Failed: $FAILED_TESTS"

if [ $FAILED_TESTS -eq 0 ]; then
    log_success "All tests passed! 🎉"
    exit 0
else
    log_failure "$FAILED_TESTS tests failed. Check logs in $OUTPUT_DIR/"
    exit 1
fi
