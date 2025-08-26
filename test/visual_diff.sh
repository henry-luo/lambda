#!/bin/bash

# Visual diff script for Lambda Script tests with enhanced output
# Shows side-by-side diffs for failed tests

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Options
SHOW_DIFF_DETAILS=false
USE_GIT_DIFF=true

show_help() {
    echo "Visual Test Diff Script for Lambda Scripts"
    echo ""
    echo "Usage: ./test/visual_diff.sh [options] [test_name]"
    echo ""
    echo "NOTE: Run this script from the project root directory"
    echo ""
    echo "Options:"
    echo "  -d, --details         Show detailed diff output for failed tests"
    echo "  -g, --git-diff        Use git diff for colored output (default)"
    echo "  -s, --side-by-side    Use side-by-side diff format"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "Arguments:"
    echo "  test_name            Optional: specific test to compare (without .ls extension)"
    echo ""
    echo "Examples:"
    echo "  ./test/visual_diff.sh                    # Compare all tests"
    echo "  ./test/visual_diff.sh value             # Compare only value.ls test"
    echo "  ./test/visual_diff.sh -d value          # Show detailed diff for value.ls test"
    echo "  ./test/visual_diff.sh -s                # Use side-by-side format for all tests"
}

# Parse arguments
SPECIFIC_TEST=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--details)
            SHOW_DIFF_DETAILS=true
            shift
            ;;
        -g|--git-diff)
            USE_GIT_DIFF=true
            shift
            ;;
        -s|--side-by-side)
            USE_GIT_DIFF=false
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
        *)
            SPECIFIC_TEST="$1"
            shift
            ;;
    esac
done

echo -e "${BLUE}Lambda Script Visual Test Diff${NC}"
echo -e "${YELLOW}==============================${NC}"
echo ""

# Function to show diff for a specific test
show_test_diff() {
    local test_file="$1"
    local base_name=$(basename "$test_file" .ls)
    local expected_file="./test/lambda/${base_name}.txt"
    local actual_file="./test_output/${base_name}.txt"
    
    echo -e "${CYAN}Testing: ${base_name}.ls${NC}"
    echo "Expected: $expected_file"
    echo "Actual: $actual_file"
    
    # Check if files exist
    if [[ ! -f "$expected_file" ]]; then
        echo -e "${YELLOW}SKIP: No expected output file${NC}"
        return
    fi
    
    if [[ ! -f "$actual_file" ]]; then
        echo -e "${RED}FAIL: No actual output file${NC}"
        return
    fi
    
    # Compare files
    if diff -q "$expected_file" "$actual_file" > /dev/null 2>&1; then
        echo -e "${GREEN}PASS: Files match${NC}"
        return
    fi
    
    echo -e "${RED}FAIL: Files differ${NC}"
    
    if [[ "$SHOW_DIFF_DETAILS" == true ]]; then
        echo ""
        echo -e "${YELLOW}Differences:${NC}"
        echo "----------------------------------------"
        
        if [[ "$USE_GIT_DIFF" == true ]]; then
            # Use git diff for colored output
            git diff --no-index --color=always --word-diff=color "$expected_file" "$actual_file" 2>/dev/null || \
            git diff --no-index --color=always "$expected_file" "$actual_file" 2>/dev/null
        else
            # Use side-by-side diff
            diff -y --width=120 "$expected_file" "$actual_file" || true
        fi
        
        echo "----------------------------------------"
    fi
    
    echo ""
}

# If specific test is provided, show only that test
if [[ -n "$SPECIFIC_TEST" ]]; then
    test_file="./test/lambda/${SPECIFIC_TEST}.ls"
    if [[ -f "$test_file" ]]; then
        SHOW_DIFF_DETAILS=true  # Always show details for specific test
        show_test_diff "$test_file"
    else
        echo -e "${RED}Error: Test file $test_file not found${NC}"
        exit 1
    fi
    exit 0
fi

# Process all tests in lambda directory
failed_count=0
passed_count=0
total_count=0

for ls_file in ./test/lambda/*.ls; do
    [[ -f "$ls_file" ]] || continue
    
    base_name=$(basename "$ls_file" .ls)
    expected_file="./test/lambda/${base_name}.txt"
    
    # Only process tests with expected output files
    [[ -f "$expected_file" ]] || continue
    
    ((total_count++))
    
    if [[ "$SHOW_DIFF_DETAILS" == true ]]; then
        show_test_diff "$ls_file"
    else
        # Quick summary
        actual_file="./test_output/${base_name}.txt"
        
        if [[ ! -f "$actual_file" ]]; then
            echo -e "${base_name}: ${RED}FAIL (missing actual)${NC}"
            ((failed_count++))
        elif diff -q "$expected_file" "$actual_file" > /dev/null 2>&1; then
            echo -e "${base_name}: ${GREEN}PASS${NC}"
            ((passed_count++))
        else
            echo -e "${base_name}: ${RED}FAIL${NC}"
            ((failed_count++))
        fi
    fi
done

echo ""
echo -e "${YELLOW}Summary:${NC}"
echo "Total tests: $total_count"
echo -e "Passed: ${GREEN}$passed_count${NC}"
echo -e "Failed: ${RED}$failed_count${NC}"

if [[ $failed_count -gt 0 && "$SHOW_DIFF_DETAILS" == false ]]; then
    echo ""
    echo -e "${YELLOW}To see detailed diffs, run:${NC}"
    echo "  ./test/visual_diff.sh --details"
    echo ""
    echo -e "${YELLOW}To see diff for a specific test:${NC}"
    echo "  ./test/visual_diff.sh --details test_name"
    echo ""
    echo -e "${YELLOW}For consolidated diff report, run:${NC}"
    echo "  ./test/test_diff.sh"
fi
