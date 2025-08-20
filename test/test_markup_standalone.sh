#!/usr/bin/env bash

# Standalone script to compile and run test_markup_roundtrip.c
# Extracted from test_all.sh configuration

set -e  # Exit on any error

# Configuration based on test_config.json for "input" suite
TEST_SOURCE="test/test_markup_roundtrip.c"
TEST_BINARY="test/test_markup_roundtrip.exe"
SPECIAL_FLAGS="-fms-extensions"

# Dependencies from test_config.json (third entry in the input suite)
DEPENDENCIES="lib/file.c build/print.o build/strview.o build/transpile.o build/utf.o build/build_ast.o build/lambda-eval.o build/lambda-mem.o build/runner.o build/mir.o build/url.o build/parse.o build/parser.o build/num_stack.o build/input*.o build/format*.o build/strbuf.o build/hashmap.o build/arraylist.o build/variable.o build/buffer.o build/utils.o build/mime-detect.o build/mime-types.o lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a -Ilib/mem-pool/include -L/opt/homebrew/lib -lgmp -L/usr/local/lib /usr/local/lib/libmir.a /usr/local/lib/liblexbor_static.a"

# Parse command line arguments
RAW_OUTPUT=false
KEEP_EXE=false
SHOW_HELP=false
JOBS=1  # Default to sequential execution

# Parse arguments
for arg in "$@"; do
    case $arg in
        --raw)
            RAW_OUTPUT=true
            shift
            ;;
        --keep-exe)
            KEEP_EXE=true
            shift
            ;;
        --jobs=*)
            JOBS="${arg#*=}"
            shift
            ;;
        --parallel)
            JOBS=0  # 0 means use all available cores
            shift
            ;;
        --help|-h)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "Unknown argument: $arg"
            SHOW_HELP=true
            ;;
    esac
done

# Show help if requested
if [ "$SHOW_HELP" = true ]; then
    echo "Standalone Test Script for test_markup_roundtrip.c"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --raw             Run test executable directly without shell wrapper"
    echo "  --keep-exe        Keep test executable after running (don't delete)"
    echo "  --jobs=N          Run tests with N parallel jobs (default: 1 for sequential)"
    echo "  --parallel        Run tests with maximum parallelism (auto-detect cores)"
    echo "  --help, -h        Show this help message"
    echo ""
    echo "This script compiles and runs test_markup_roundtrip.c with the same"
    echo "configuration as used in test_all.sh for the 'input' test suite."
    echo ""
    echo "By default, tests run sequentially (--jobs 1) to avoid race conditions"
    echo "and ensure stable output. Use --parallel for faster execution."
    exit 0
fi

# Color output functions
print_status() {
    echo -e "\033[1;34m$1\033[0m"
}

print_success() {
    echo -e "\033[1;32m$1\033[0m"
}

print_error() {
    echo -e "\033[1;31m$1\033[0m"
}

print_warning() {
    echo -e "\033[1;33m$1\033[0m"
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
        print_error "❌ Criterion testing framework not found!"
        print_error "Please install Criterion with: brew install criterion"
        exit 1
    fi
}

# Function to check for required files
check_dependencies() {
    # Check if main source file exists
    if [ ! -f "$TEST_SOURCE" ]; then
        print_error "❌ Test source file not found: $TEST_SOURCE"
        exit 1
    fi

    # Check if build directory and critical build artifacts exist
    if [ ! -d "build" ]; then
        print_error "❌ Build directory not found. Please run 'make' first to build the project."
        exit 1
    fi

    # Check for some critical build artifacts
    critical_files=(
        "build/print.o"
        "build/lambda-eval.o" 
        "build/input-markup.o"
        "lambda/tree-sitter-lambda/libtree-sitter-lambda.a"
        "lambda/tree-sitter/libtree-sitter.a"
    )

    for file in "${critical_files[@]}"; do
        if [ ! -f "$file" ]; then
            print_error "❌ Required build artifact not found: $file"
            print_error "Please run 'make' first to build the project."
            exit 1
        fi
    done

    print_status "✅ All required dependencies found"
}

# Function to compile the test
compile_test() {
    print_status "🔧 Compiling $TEST_SOURCE..."

    # Always clean up existing executable to force rebuild without caching
    if [ -f "$TEST_BINARY" ]; then
        rm -f "$TEST_BINARY"
        print_status "🧹 Removed existing executable to force clean rebuild"
    fi

    # Build the compilation command based on build_dependency_based_compile_cmd from test_all.sh
    local compile_cmd="gcc -std=c99 -Wall -Wextra -g -O0 -I. -Ilambda -Ilib $CRITERION_FLAGS -o $TEST_BINARY $TEST_SOURCE $DEPENDENCIES $SPECIAL_FLAGS"
    
    if [ "$RAW_OUTPUT" = true ]; then
        echo "Compilation command:"
        echo "$compile_cmd"
        echo ""
    fi

    # Execute compilation
    if ! $compile_cmd; then
        print_error "❌ Compilation failed!"
        exit 1
    fi

    print_success "✅ Compilation successful: $TEST_BINARY"
}

# Function to run the test
run_test() {
    if [ ! -f "$TEST_BINARY" ]; then
        print_error "❌ Test binary not found: $TEST_BINARY"
        exit 1
    fi

    if [ "$JOBS" -eq 1 ]; then
        print_status "🚀 Running markup roundtrip tests sequentially..."
    elif [ "$JOBS" -eq 0 ]; then
        print_status "🚀 Running markup roundtrip tests in parallel (auto-detect cores)..."
    else
        print_status "🚀 Running markup roundtrip tests with $JOBS parallel jobs..."
    fi
    echo ""

    # Build the test command with job control
    local test_cmd="./$TEST_BINARY"
    if [ "$JOBS" -eq 0 ]; then
        # Let Criterion auto-detect the number of cores
        test_cmd="$test_cmd --jobs 0"
    else
        # Use specified number of jobs
        test_cmd="$test_cmd --jobs $JOBS"
    fi

    if [ "$RAW_OUTPUT" = true ]; then
        # Run the test directly with job control
        $test_cmd
    else
        # Run with formatted output and job control
        if $test_cmd; then
            print_success "✅ All tests passed!"
        else
            print_error "❌ Some tests failed!"
            exit 1
        fi
    fi
}

# Function to cleanup
cleanup() {
    if [ "$KEEP_EXE" != true ] && [ -f "$TEST_BINARY" ]; then
        rm -f "$TEST_BINARY"
        print_status "🧹 Cleaned up test executable"
    fi
}

# Main execution
echo ""
print_status "================================================"
print_status "     Standalone Markup Roundtrip Test Runner    "
print_status "================================================"
echo ""

# Check if we're in the right directory and navigate to project root if needed
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Navigate to project root for execution
cd "$PROJECT_ROOT"

if [ ! -f "Makefile" ] || [ ! -d "lambda" ]; then
    print_error "❌ Cannot locate project root directory with Makefile and lambda folder"
    print_error "❌ Please ensure this script is in the test/ directory of the project"
    exit 1
fi

print_status "Working from project root: $(pwd)"

# Build the project first
print_status "🔨 Building project with make..."
if ! make; then
    print_error "❌ Project build failed!"
    print_error "Please check the build errors above and fix any issues."
    exit 1
fi
print_success "✅ Project build completed successfully"
echo ""

# Find Criterion installation
find_criterion

# Check dependencies
check_dependencies

# Compile test
compile_test

# Run test
run_test

# Cleanup
trap cleanup EXIT

print_success "🎉 Markup roundtrip test completed successfully!"
