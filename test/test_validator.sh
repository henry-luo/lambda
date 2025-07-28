#!/bin/bash

# Lambda Validator Comprehensive Criterion Test Suite Runner
# Compiles and runs all validator tests as proper Criterion unit tests

set -e  # Exit on any error

echo "================================================"
echo " Lambda Validator Criterion Test Suite Runner "
echo "================================================"

# Configuration
TEST_SOURCES="test/test_validator.c"
TEST_BINARY="test/test_validator"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}$1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Check if lambda executable exists
if [ ! -f "./lambda.exe" ]; then
    print_error "Lambda executable not found. Run 'make' first."
    exit 1
fi

print_success "Lambda executable ready"

# Compile the Criterion-based test suite with proper linking
print_status "🔧 Compiling Criterion test suite with validator integration..."

# Find Criterion installation
CRITERION_FLAGS=""
if pkg-config --exists criterion 2>/dev/null; then
    CRITERION_FLAGS=$(pkg-config --cflags --libs criterion)
    print_status "Found Criterion via pkg-config"
elif [ -d "/opt/homebrew/include/criterion" ]; then
    CRITERION_FLAGS="-I/opt/homebrew/include -L/opt/homebrew/lib -lcriterion"
    print_status "Found Criterion via Homebrew (Apple Silicon)"
elif [ -d "/usr/local/include/criterion" ]; then
    CRITERION_FLAGS="-I/usr/local/include -L/usr/local/lib -lcriterion"
    print_status "Found Criterion via Homebrew (Intel)"
else
    print_error "Criterion testing framework not found!"
    print_error "Please install Criterion:"
    print_error "  macOS: brew install criterion"
    print_error "  Ubuntu: sudo apt-get install libcriterion-dev"
    exit 1
fi

# Compile with full validator integration
COMPILE_CMD="gcc -std=c99 -Wall -Wextra -g \
    -Iinclude -Ilambda -Ilambda/validator \
    $CRITERION_FLAGS \
    $TEST_SOURCES \
    -o $TEST_BINARY"

print_status "Compile command: $COMPILE_CMD"

if $COMPILE_CMD 2>/dev/null; then
    print_success "Criterion test suite compiled successfully"
else
    print_error "Failed to compile Criterion test suite"
    print_error "Attempting compilation with detailed error output..."
    $COMPILE_CMD
    exit 1
fi

# Run the comprehensive Criterion test suite
print_status "🧪 Running comprehensive Criterion validator tests..."
echo ""

# Set environment variables for test files
export TEST_DIR_PATH="$PWD/test/lambda/validator"
export LAMBDA_EXE_PATH="$PWD/lambda.exe"

# Run tests with detailed output
set +e  # Disable strict error handling for test execution
TEST_OUTPUT=$(./"$TEST_BINARY" --verbose --tap 2>&1)
TEST_EXIT_CODE=$?
set -e  # Re-enable strict error handling

# Parse and display results
echo "$TEST_OUTPUT"
echo ""

# Check if all tests passed regardless of exit code
TOTAL_TESTS=$(echo "$TEST_OUTPUT" | grep -c "^ok " 2>/dev/null || echo "0")
FAILED_TESTS=$(echo "$TEST_OUTPUT" | grep -c "^not ok " 2>/dev/null || echo "0")

# Clean numeric values
TOTAL_TESTS=$(echo "$TOTAL_TESTS" | tr -cd '0-9')
FAILED_TESTS=$(echo "$FAILED_TESTS" | tr -cd '0-9')

# Ensure defaults
TOTAL_TESTS=${TOTAL_TESTS:-0}
FAILED_TESTS=${FAILED_TESTS:-0}

if [ "$FAILED_TESTS" -eq 0 ] && [ "$TOTAL_TESTS" -gt 0 ]; then
    print_success "🎉 ALL CRITERION TESTS PASSED!"
    
    # Count test results
    SKIPPED_TESTS=$(echo "$TEST_OUTPUT" | grep -c "^ok .* # SKIP" 2>/dev/null || echo "0") 
    SKIPPED_TESTS=$(echo "$SKIPPED_TESTS" | tr -cd '0-9')
    SKIPPED_TESTS=${SKIPPED_TESTS:-0}
    PASSED_TESTS=$((TOTAL_TESTS - SKIPPED_TESTS))
    
    echo ""
    print_status "📊 Test Results Summary:"
    echo "   Total Tests: $TOTAL_TESTS"
    echo "   Passed: $PASSED_TESTS"
    echo "   Skipped: $SKIPPED_TESTS"
    echo "   Failed: 0"
    
    echo ""
    print_success "🎉 All tests passed! Lambda Validator is ready for production use."
    echo "📋 See lambda/validator/validator.md for detailed test coverage information."
    
    # Cleanup
    if [ -f "$TEST_BINARY" ]; then
        rm "$TEST_BINARY"
    fi
    
    exit 0
else
    TOTAL_TESTS_NOT_OK=$(echo "$TEST_OUTPUT" | grep -c "^not ok " 2>/dev/null || echo "0")
    
    # Clean and ensure numeric values
    TOTAL_TESTS_NOT_OK=$(echo "$TOTAL_TESTS_NOT_OK" | tr -cd '0-9')
    TOTAL_TESTS_NOT_OK=${TOTAL_TESTS_NOT_OK:-0}
    
    TOTAL_TESTS_CALCULATED=$((TOTAL_TESTS + TOTAL_TESTS_NOT_OK))
    PASSED_TESTS=$((TOTAL_TESTS_CALCULATED - FAILED_TESTS))
    
    print_warning "Some Criterion tests failed"
    echo ""
    print_status "📊 Test Results Summary:"
    echo "   Total Tests: $TOTAL_TESTS_CALCULATED"
    echo "   Passed: $PASSED_TESTS"
    echo "   Failed: $FAILED_TESTS"
    
    echo ""
    print_warning "⚠️  Some tests failed - see lambda/validator/validator.md for test details"
    echo ""
    echo "✅ Criterion testing framework operational"
    echo "📋 See lambda/validator/validator.md for comprehensive test coverage information"
    
    # Cleanup
    if [ -f "$TEST_BINARY" ]; then
        rm "$TEST_BINARY"
    fi
    
    exit 1
fi
