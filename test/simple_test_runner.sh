#!/bin/bash

# Simple test runner for Lambda standard tests
# This is a workaround for macOS compatibility issues with the C++ test runner

echo "Running Lambda Standard Tests (simple runner)..."

test_dir="test/std"
output_dir="test_output"

# Create output directory if it doesn't exist
mkdir -p "$output_dir"

# Counters
total_tests=0
passed_tests=0
failed_tests=0

# Create a temporary file to store results
temp_file=$(mktemp)

# Find all .ls test files
while IFS= read -r test_file; do
    test_name=$(basename "$test_file" .ls)
    echo "Running test: $test_name"
    
    # Run the test with lambda.exe
    if ./lambda.exe "$test_file" > "$output_dir/${test_name}.out" 2>&1; then
        echo "  ✓ PASS: $test_name"
        ((passed_tests++))
    else
        echo "  ✗ FAIL: $test_name (see $output_dir/${test_name}.out)"
        ((failed_tests++))
    fi
    
    ((total_tests++))
done < <(find "$test_dir" -name "*.ls" | sort)

# Clean up
rm -f "$temp_file"

# Print summary
echo ""
echo "Test Summary:"
echo "  Total:  $total_tests"
echo "  Passed: $passed_tests"
echo "  Failed: $failed_tests"

# Exit with non-zero status if any tests failed
if [ $failed_tests -gt 0 ]; then
    exit 1
fi

exit 0
