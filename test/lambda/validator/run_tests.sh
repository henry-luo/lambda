#!/bin/bash

# Lambda Schema Validator Test Runner
# Author: Henry Luo

set -e

echo "=== Lambda Schema Validator Test Suite ==="
echo "Setting up test environment..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [[ ! -f "Makefile" ]] || [[ ! -f "test_validator_basic.c" ]]; then
    print_error "Please run this script from the validator_tests directory"
    exit 1
fi

# Check for required dependencies
print_status "Checking dependencies..."

if ! command -v clang &> /dev/null; then
    print_error "clang compiler not found. Please install clang."
    exit 1
fi

if ! command -v make &> /dev/null; then
    print_error "make not found. Please install make."
    exit 1
fi

# Clean previous builds
print_status "Cleaning previous builds..."
make clean 2>/dev/null || true

# Build test suite
print_status "Building test suite..."
if make all; then
    print_success "Test suite built successfully"
else
    print_error "Failed to build test suite"
    exit 1
fi

# Run basic tests
print_status "Running basic validator tests..."
if make test-basic; then
    print_success "Basic tests completed"
    BASIC_SUCCESS=1
else
    print_warning "Basic tests had some failures"
    BASIC_SUCCESS=0
fi

echo ""

# Run advanced tests
print_status "Running advanced validator tests with error recovery..."
if make test-advanced; then
    print_success "Advanced tests completed"
    ADVANCED_SUCCESS=1
else
    print_warning "Advanced tests had some failures"
    ADVANCED_SUCCESS=0
fi

echo ""

# Summary
print_status "Test Summary:"
if [[ $BASIC_SUCCESS -eq 1 ]]; then
    echo -e "  Basic Tests:    ${GREEN}PASSED${NC}"
else
    echo -e "  Basic Tests:    ${RED}FAILED${NC}"
fi

if [[ $ADVANCED_SUCCESS -eq 1 ]]; then
    echo -e "  Advanced Tests: ${GREEN}PASSED${NC}"
else
    echo -e "  Advanced Tests: ${RED}FAILED${NC}"
fi

# Overall result
if [[ $BASIC_SUCCESS -eq 1 && $ADVANCED_SUCCESS -eq 1 ]]; then
    echo ""
    print_success "🎉 All validator tests passed!"
    echo ""
    print_status "The Lambda Schema Validator is working correctly with:"
    echo "  ✓ Basic type validation"
    echo "  ✓ Complex schema validation"
    echo "  ✓ Error recovery and continuation"
    echo "  ✓ Enhanced error reporting with context paths"
    echo "  ✓ Union and array validation"
    echo "  ✓ Memory management"
    echo ""
    exit 0
else
    echo ""
    print_warning "Some tests failed. Please review the output above."
    echo ""
    print_status "Next steps for enhancement:"
    if [[ $BASIC_SUCCESS -eq 0 ]]; then
        echo "  - Fix basic validation issues"
    fi
    if [[ $ADVANCED_SUCCESS -eq 0 ]]; then
        echo "  - Review advanced error recovery logic"
        echo "  - Check path tracking implementation"
    fi
    echo "  - Review error messages and suggestions"
    echo "  - Optimize performance if needed"
    echo ""
    exit 1
fi
