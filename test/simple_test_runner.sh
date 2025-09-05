#!/bin/bash

# Simple test runner for Lambda standard tests
# This is a workaround for macOS compatibility issues with the C++ test runner

echo "Running Lambda Standard Tests (simple runner)..."

test_dir="test/std"
output_dir="test_output"

export LANG=C.UTF-8
export LC_ALL=C.UTF-8

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create output directory if it doesn't exist
mkdir -p "$output_dir"

# Counters
total_tests=0
passed_tests=0
failed_tests=0

# Find all .ls test files
while IFS= read -r test_file; do
    test_name=$(basename "$test_file" .ls)
    test_path=$(dirname "$test_file")
    expected_file="${test_file%.ls}.expected"
    
    echo -n "Running test: $test_name"
    
    # Run the test with lambda.exe and capture output
    ./lambda.exe "$test_file" > "$output_dir/${test_name}.out" 2>&1
    test_status=$?
    
    if [ $test_status -ne 0 ]; then
        echo -e "  ${RED}✗ FAIL${NC} (exit code: $test_status)"
        echo "  See $output_dir/${test_name}.out for details"
        ((failed_tests++))
    else
        # Check if there's an expected file
        if [ -f "$expected_file" ]; then
            # Compare output with expected
            if diff -u "$expected_file" "$output_dir/${test_name}.out" > /dev/null 2>&1; then
                echo -e "  ${GREEN}✓ PASS${NC}"
                ((passed_tests++))
            else
                echo -e "  ${RED}✗ FAIL${NC} (output mismatch)"
                echo "  Expected output in: $expected_file"
                echo "  Actual output in:   $output_dir/${test_name}.out"
                echo "  Diff:"
                diff -u "$expected_file" "$output_dir/${test_name}.out" | sed 's/^/    /'
                ((failed_tests++))
            fi
        else
            # No expected file, just check exit status
            echo -e "  ${YELLOW}? PASS (no .expected file)${NC}"
            ((passed_tests++))
        fi
    fi
    
    ((total_tests++))
done < <(find "$test_dir" -name "*.ls" | sort)

# Print summary
echo -e "\nTest Summary:"
echo -e "  Total:  $total_tests"
echo -e "  ${GREEN}Passed: $passed_tests${NC}"

if [ $failed_tests -gt 0 ]; then
    echo -e "  ${RED}Failed: $failed_tests${NC}"
else
    echo -e "  ${GREEN}All tests passed!${NC}"
fi

# Exit with non-zero status if any tests failed
if [ $failed_tests -gt 0 ]; then
    exit 1
fi

exit 0
