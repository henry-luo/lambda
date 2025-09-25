#!/usr/bin/env bash

# Radiant Flex Layout Test Runner
# This script builds and runs all flex layout tests

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🎨 Radiant Flex Layout Test Suite${NC}"
echo "=================================="

# Check if we're in the right directory
if [ ! -f "build_lambda_config.json" ]; then
    echo -e "${RED}Error: Must be run from lambda project root directory${NC}"
    exit 1
fi

# Create test output directory
mkdir -p test_output

# Function to run a single test
run_test() {
    local test_name="$1"
    local test_binary="$2"
    
    echo -e "\n${YELLOW}Running $test_name...${NC}"
    
    if [ -f "$test_binary" ]; then
        if ./"$test_binary" --gtest_output=xml:test_output/${test_name}_results.xml; then
            echo -e "${GREEN}✓ $test_name passed${NC}"
            return 0
        else
            echo -e "${RED}✗ $test_name failed${NC}"
            return 1
        fi
    else
        echo -e "${RED}✗ Test binary $test_binary not found${NC}"
        echo "   Run 'make test' or build the tests first"
        return 1
    fi
}

# Function to build tests
build_tests() {
    echo -e "\n${YELLOW}Building flex layout tests...${NC}"
    
    # Check if we have a Makefile or build system
    if [ -f "Makefile" ]; then
        make test-radiant
    elif [ -f "premake5.lua" ]; then
        premake5 gmake2
        make config=debug test-radiant
    else
        echo -e "${RED}No build system found. Please build tests manually.${NC}"
        return 1
    fi
}

# Parse command line arguments
BUILD_TESTS=false
RUN_SPECIFIC=""
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--build)
            BUILD_TESTS=true
            shift
            ;;
        -t|--test)
            RUN_SPECIFIC="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -b, --build     Build tests before running"
            echo "  -t, --test NAME Run specific test (basic|algorithm|integration)"
            echo "  -v, --verbose   Verbose output"
            echo "  -h, --help      Show this help"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Build tests if requested
if [ "$BUILD_TESTS" = true ]; then
    build_tests
fi

# Test results tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Define test configurations
TESTS_basic="test_radiant_flex_gtest.exe"
TESTS_algorithm="test_radiant_flex_algorithm_gtest.exe"
TESTS_integration="test_radiant_flex_integration_gtest.exe"

# Run specific test or all tests
if [ -n "$RUN_SPECIFIC" ]; then
    case "$RUN_SPECIFIC" in
        "basic")
            TOTAL_TESTS=1
            if run_test "basic" "$TESTS_basic"; then
                PASSED_TESTS=1
            else
                FAILED_TESTS=1
            fi
            ;;
        "algorithm")
            TOTAL_TESTS=1
            if run_test "algorithm" "$TESTS_algorithm"; then
                PASSED_TESTS=1
            else
                FAILED_TESTS=1
            fi
            ;;
        "integration")
            TOTAL_TESTS=1
            if run_test "integration" "$TESTS_integration"; then
                PASSED_TESTS=1
            else
                FAILED_TESTS=1
            fi
            ;;
        *)
            echo -e "${RED}Error: Unknown test '$RUN_SPECIFIC'${NC}"
            echo "Available tests: basic, algorithm, integration"
            exit 1
            ;;
    esac
else
    # Run all tests
    for test_name in basic algorithm integration; do
        TOTAL_TESTS=$((TOTAL_TESTS + 1))
        case "$test_name" in
            "basic")
                test_exe="$TESTS_basic"
                ;;
            "algorithm")
                test_exe="$TESTS_algorithm"
                ;;
            "integration")
                test_exe="$TESTS_integration"
                ;;
        esac
        
        if run_test "$test_name" "$test_exe"; then
            PASSED_TESTS=$((PASSED_TESTS + 1))
        else
            FAILED_TESTS=$((FAILED_TESTS + 1))
        fi
    done
fi

# Print summary
echo -e "\n${BLUE}Test Summary${NC}"
echo "============"
echo -e "Total tests: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
else
    echo -e "Failed: $FAILED_TESTS"
fi

# Generate combined test report
if [ -d "test_output" ] && [ "$(ls -A test_output/*.xml 2>/dev/null)" ]; then
    echo -e "\n${YELLOW}Test reports generated in test_output/${NC}"
    ls -la test_output/*.xml
fi

# Exit with appropriate code
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "\n${RED}Some tests failed!${NC}"
    exit 1
else
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
fi
