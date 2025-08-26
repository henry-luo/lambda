#!/bin/bash

# Enhanced shell script to compare test script outputs with expected results
# Loops through all *.ls test scripts and compares their outputs with expected *.txt files

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default options
ONLY_WITH_EXPECTED=false
VERBOSE=false

# Function to show help
show_help() {
    echo "Test Diff Script for Lambda Scripts"
    echo ""
    echo "Usage: ./test/test_diff.sh [options]"
    echo ""
    echo "NOTE: Run this script from the project root directory"
    echo ""
    echo "Options:"
    echo "  -e, --expected-only    Only process tests that have expected output files"
    echo "  -v, --verbose          Show verbose output including file paths"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "This script:"
    echo "  1. Finds all *.ls files in ./test/lambda/ directory"
    echo "  2. For each script, looks for a corresponding *.txt expected output file"
    echo "  3. Compares expected output with actual output in ./test_output/"
    echo "  4. Generates a consolidated diff report in ./test_output/test_diff.txt"
    echo ""
    echo "Examples:"
    echo "  ./test/test_diff.sh"
    echo "  ./test/test_diff.sh --expected-only"
    echo ""
    echo "Exit codes:"
    echo "  0 - All tests passed"
    echo "  1 - One or more tests failed"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -e|--expected-only)
            ONLY_WITH_EXPECTED=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Initialize counters using temporary files to work around subshell variable scope
TEMP_DIR=$(mktemp -d)
echo "0" > "$TEMP_DIR/total_tests"
echo "0" > "$TEMP_DIR/failed_tests"
echo "0" > "$TEMP_DIR/passed_tests"
echo "0" > "$TEMP_DIR/missing_expected"
echo "0" > "$TEMP_DIR/missing_actual"

# Ensure test_output directory exists
mkdir -p ./test_output

# Initialize the consolidated diff file
DIFF_FILE="./test_output/test_diff.txt"
echo "Lambda Script Test Diff Report" > "$DIFF_FILE"
echo "Generated on: $(date)" >> "$DIFF_FILE"
if [[ "$ONLY_WITH_EXPECTED" == true ]]; then
    echo "Mode: Only processing tests with expected output files" >> "$DIFF_FILE"
fi
echo "=======================================" >> "$DIFF_FILE"
echo "" >> "$DIFF_FILE"

echo -e "${BLUE}Lambda Script Test Diff Comparison${NC}"
echo -e "${YELLOW}=====================================${NC}"
if [[ "$ONLY_WITH_EXPECTED" == true ]]; then
    echo -e "${BLUE}Mode:${NC} Only processing tests with expected output files"
fi
echo "Results will be consolidated in: $DIFF_FILE"
echo ""

# Find all .ls files in test/lambda directory only (no subdirectories)
find ./test/lambda -maxdepth 1 -name "*.ls" -type f | sort | while read -r ls_file; do
    # Get the base name without extension and directory
    base_name=$(basename "$ls_file" .ls)
    test_dir=$(dirname "$ls_file")
    
    # Construct path to expected output file
    expected_file="${test_dir}/${base_name}.txt"
    
    # Construct path to actual output file
    actual_file="./test_output/${base_name}.txt"
    
    # Skip if only processing tests with expected outputs and no expected file exists
    if [[ "$ONLY_WITH_EXPECTED" == true ]] && [[ ! -f "$expected_file" ]]; then
        continue
    fi
    
    if [[ "$VERBOSE" == true ]]; then
        echo "Processing: $ls_file"
        echo "  Expected: $expected_file"
        echo "  Actual: $actual_file"
    else
        echo "Processing: $ls_file"
    fi
    
    # Check if expected output file exists
    if [[ ! -f "$expected_file" ]]; then
        echo -e "  ${YELLOW}SKIP${NC}: No expected output file"
        
        missing_expected=$(cat "$TEMP_DIR/missing_expected")
        missing_expected=$((missing_expected + 1))
        echo "$missing_expected" > "$TEMP_DIR/missing_expected"
        
        # Log to diff file
        echo "MISSING EXPECTED: $ls_file" >> "$DIFF_FILE"
        echo "  Expected file: $expected_file (not found)" >> "$DIFF_FILE"
        echo "" >> "$DIFF_FILE"
        continue
    fi
    
    # Update counters using temporary files (only count tests with expected outputs)
    total_tests=$(cat "$TEMP_DIR/total_tests")
    total_tests=$((total_tests + 1))
    echo "$total_tests" > "$TEMP_DIR/total_tests"
    
    # Check if actual output file exists
    if [[ ! -f "$actual_file" ]]; then
        echo -e "  ${RED}FAIL${NC}: No actual output file"
        
        missing_actual=$(cat "$TEMP_DIR/missing_actual")
        missing_actual=$((missing_actual + 1))
        echo "$missing_actual" > "$TEMP_DIR/missing_actual"
        
        failed_tests=$(cat "$TEMP_DIR/failed_tests")
        failed_tests=$((failed_tests + 1))
        echo "$failed_tests" > "$TEMP_DIR/failed_tests"
        
        # Log to diff file
        echo "MISSING ACTUAL: $ls_file" >> "$DIFF_FILE"
        echo "  Expected file: $expected_file" >> "$DIFF_FILE"
        echo "  Actual file: $actual_file (not found)" >> "$DIFF_FILE"
        echo "" >> "$DIFF_FILE"
        continue
    fi
    
    # Compare the files
    if diff -u "$expected_file" "$actual_file" > /dev/null 2>&1; then
        echo -e "  ${GREEN}PASS${NC}: Output matches expected"
        
        passed_tests=$(cat "$TEMP_DIR/passed_tests")
        passed_tests=$((passed_tests + 1))
        echo "$passed_tests" > "$TEMP_DIR/passed_tests"
    else
        echo -e "  ${RED}FAIL${NC}: Output differs from expected"
        
        failed_tests=$(cat "$TEMP_DIR/failed_tests")
        failed_tests=$((failed_tests + 1))
        echo "$failed_tests" > "$TEMP_DIR/failed_tests"
        
        # Log detailed diff to consolidated file
        echo "DIFF FAILURE: $ls_file" >> "$DIFF_FILE"
        echo "  Expected: $expected_file" >> "$DIFF_FILE"
        echo "  Actual: $actual_file" >> "$DIFF_FILE"
        echo "  Diff:" >> "$DIFF_FILE"
        echo "----------------------------------------" >> "$DIFF_FILE"
        diff -u "$expected_file" "$actual_file" >> "$DIFF_FILE" 2>&1
        echo "----------------------------------------" >> "$DIFF_FILE"
        echo "" >> "$DIFF_FILE"
    fi
done

# Read final counter values
total_tests=$(cat "$TEMP_DIR/total_tests")
failed_tests=$(cat "$TEMP_DIR/failed_tests")
passed_tests=$(cat "$TEMP_DIR/passed_tests")
missing_expected=$(cat "$TEMP_DIR/missing_expected")
missing_actual=$(cat "$TEMP_DIR/missing_actual")

# Print summary
echo ""
echo -e "${YELLOW}Test Diff Summary:${NC}"
echo "==================="
echo "Total tests processed: $total_tests"
echo -e "Passed: ${GREEN}$passed_tests${NC}"
echo -e "Failed: ${RED}$failed_tests${NC}"
echo -e "Missing expected files: ${YELLOW}$missing_expected${NC}"
echo -e "Missing actual files: ${RED}$missing_actual${NC}"

# Add summary to diff file
echo "" >> "$DIFF_FILE"
echo "SUMMARY" >> "$DIFF_FILE"
echo "=======" >> "$DIFF_FILE"
echo "Total tests processed: $total_tests" >> "$DIFF_FILE"
echo "Passed: $passed_tests" >> "$DIFF_FILE"
echo "Failed: $failed_tests" >> "$DIFF_FILE"
echo "Missing expected files: $missing_expected" >> "$DIFF_FILE"
echo "Missing actual files: $missing_actual" >> "$DIFF_FILE"
echo "" >> "$DIFF_FILE"
echo "Report generated on: $(date)" >> "$DIFF_FILE"

echo ""
echo "Consolidated diff report saved to: $DIFF_FILE"

# Show quick stats if there are failures
if [[ $failed_tests -gt 0 ]]; then
    echo ""
    echo -e "${RED}Failed tests details:${NC}"
    grep "^DIFF FAILURE\|^MISSING ACTUAL" "$DIFF_FILE" | head -10
    if [[ $(grep -c "^DIFF FAILURE\|^MISSING ACTUAL" "$DIFF_FILE") -gt 10 ]]; then
        echo "... and $(($(grep -c "^DIFF FAILURE\|^MISSING ACTUAL" "$DIFF_FILE") - 10)) more failures"
    fi
    echo "See $DIFF_FILE for complete details"
fi

# Clean up temporary files
rm -rf "$TEMP_DIR"

# Exit with error code if any tests failed
if [[ $failed_tests -gt 0 ]]; then
    exit 1
else
    exit 0
fi
