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
    raw_output="$output_dir/${test_name}.raw"
    ./lambda.exe "$test_file" > "$raw_output" 2>&1
    test_status=$?
    
    # Extract actual script output (after the marker line)
    actual_output="$output_dir/${test_name}.out"
    
    # Get the actual script output (everything after the marker line)
    if grep -q '^##### Script' "$raw_output"; then
        # Extract everything after the marker line and clean it up
        sed -n '/^##### Script/,$p' "$raw_output" | \
            tail -n +2 | \
            grep -v '^#####' | \
            sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' | \
            grep -v '^$' > "$actual_output"
    else
        # No marker found, clean up the raw output
        # Filter out log lines (timestamp + log level like "15:36:18 [NOTE] ...")
        # and error/parse diagnostic lines
        grep -v -E '^[0-9]{2}:[0-9]{2}:[0-9]{2} \[' "$raw_output" | \
            grep -v -E '^PARSE ERROR:' | \
            grep -v -E '^\s+Error node has' | \
            grep -v -E '^\s+Child [0-9]+:' | \
            grep -v -E '^Error: Script execution failed:' | \
            grep -v -E '^\s+\|$' | \
            grep -v -E '^\s+\^' | \
            grep -v -E '^\s+[0-9]+ error' | \
            sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' -e '/^$/d' > "$actual_output"
    fi
    
    # Ensure the output ends with exactly one newline
    if [ -s "$actual_output" ]; then
        # Only add newline if file is not empty
        echo "" >> "$actual_output"
    fi
    
    if [ $test_status -ne 0 ]; then
        echo -e "  ${RED}✗ FAIL${NC} (exit code: $test_status)"
        echo "  See $raw_output for details"
        ((failed_tests++))
    else
        # Check if there's an expected file
        if [ -f "$expected_file" ]; then
            # Create a normalized expected output file with just the expected values
            normalized_expected="$output_dir/${test_name}.expected.norm"
            
            # Define expected outputs for each test
            case "$test_name" in
                # Use the expected file directly
                *)
                    grep -v '^[[:space:]]*$' "$expected_file" | \
                        sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' | \
                        grep -v '^$' > "$normalized_expected"
                    # Add trailing newline if file is not empty
                    [ -s "$normalized_expected" ] && echo "" >> "$normalized_expected"
                    ;;
            esac
            
            # Compare normalized output with normalized expected (ignoring whitespace and blank lines)
            if diff -u -w -B --strip-trailing-cr "$normalized_expected" "$actual_output" > /dev/null 2>&1; then
                echo -e "  ${GREEN}✓ PASS${NC}"
                ((passed_tests++))
            else
                echo -e "  ${RED}✗ FAIL${NC} (output mismatch)"
                echo "  Expected output in: $expected_file"
                echo "  Actual output in:   $actual_output"
                echo "  Diff (normalized):"
                diff -u --strip-trailing-cr "$normalized_expected" "$actual_output" | sed 's/^/    /'
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
