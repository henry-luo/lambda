#!/bin/bash

# Math Test Runner
# Compiles and runs math tests directly without using test_all.sh

set -e  # Exit on any error

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

# Function to find Criterion installation
find_criterion() {
    if pkg-config --exists criterion 2>/dev/null; then
        CRITERION_FLAGS=$(pkg-config --cflags --libs criterion)
        print_status "Found Criterion via pkg-config"
    elif [ -d "/opt/homebrew/include/criterion" ]; then
        CRITERION_FLAGS="-I/opt/homebrew/include -L/opt/homebrew/lib -lcriterion"
        print_status "Found Criterion via Homebrew (Apple Silicon)"
    elif [ -d "/usr/local/include/criterion" ]; then
        CRITERION_FLAGS="-I/usr/local/include -L/usr/local/lib -lcriterion"
        print_status "Found Criterion via Homebrew (Intel)"
    elif [ -d "/opt/homebrew/Cellar/criterion" ]; then
        # Fallback to hardcoded path for older installations
        CRITERION_PATH=$(find /opt/homebrew/Cellar/criterion -name "include" -type d | head -1)
        if [ -n "$CRITERION_PATH" ]; then
            CRITERION_LIB_PATH="${CRITERION_PATH%/include}/lib"
            CRITERION_FLAGS="-I$CRITERION_PATH -L$CRITERION_LIB_PATH -lcriterion"
            print_status "Found Criterion via Homebrew (legacy path)"
        fi
    else
        print_error "Criterion testing framework not found!"
        print_error "Please install Criterion:"
        print_error "  macOS: brew install criterion"
        print_error "  Ubuntu: sudo apt-get install libcriterion-dev"
        exit 1
    fi
}

# Main execution
print_status "================================================"
print_status "         Math Test Direct Runner              "
print_status "================================================"

# Find Criterion
find_criterion

# Math test configuration
TEST_SOURCE="test/test_math.c"
TEST_BINARY="test/test_math.exe"
TEST_DEPS="lib/file.c build/print.o build/strview.o build/transpile.o build/utf.o build/build_ast.o build/lambda-eval.o build/lambda-mem.o build/runner.o build/mir.o build/url.o build/parse.o build/parser.o build/num_stack.o build/input*.o build/format*.o build/strbuf.o build/hashmap.o build/arraylist.o build/variable.o build/buffer.o build/utils.o build/mime-detect.o build/mime-types.o lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a -Ilib/mem-pool/include -L/opt/homebrew/lib -lgmp -L/usr/local/lib /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a"

print_status "Compiling math tests..."

# Build compile command
COMPILE_CMD="gcc -std=c99 -Wall -Wextra -g -O0 -I. -Ilambda -Ilib $CRITERION_FLAGS -o $TEST_BINARY $TEST_SOURCE $TEST_DEPS"

print_status "Compile command: $COMPILE_CMD"

if $COMPILE_CMD 2>/dev/null; then
    print_success "Compiled $TEST_SOURCE successfully"
else
    print_error "Failed to compile $TEST_SOURCE"
    print_error "Attempting compilation with detailed error output..."
    $COMPILE_CMD
    exit 1
fi

print_status "🧪 Running math tests..."
echo ""

# Run the test with timeout
set +e
if command -v timeout >/dev/null 2>&1; then
    # First try with just basic output
    test_output=$(timeout 30 ./$TEST_BINARY 2>&1)
    test_exit_code=$?
    if [ $test_exit_code -eq 124 ]; then
        print_error "Math tests timed out after 30 seconds"
        exit 1
    fi
else
    # Fallback without timeout
    test_output=$(./$TEST_BINARY 2>&1)
    test_exit_code=$?
fi

# Show output
echo "$test_output"

if [ $test_exit_code -eq 0 ]; then
    print_success "Math tests completed successfully"
else
    print_error "Math tests failed with exit code: $test_exit_code"
fi

exit $test_exit_code
