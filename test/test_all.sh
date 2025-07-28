#!/bin/bash

# Lambda Comprehensive Test Suite Runner
# Compiles and runs all tests including validator and library tests as proper Criterion unit tests

set -e  # Exit on any error

echo "================================================"
echo "     Lambda Comprehensive Test Suite Runner    "
echo "================================================"

# Configuration
VALIDATOR_TEST_SOURCES="test/test_validator.c"
VALIDATOR_TEST_BINARY="test/test_validator"

LIB_TEST_SOURCES=(
    "test_strbuf.c"
    "test_strview.c" 
    "test_variable_pool.c"
    "test_num_stack.c"
    "test_mime_detect.c"
)

LIB_TEST_DEPENDENCIES=(
    "lib/strbuf.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include"
    "lib/strview.c"
    "lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include"
    "lib/num_stack.c"
    "lambda/input/mime-detect.c lambda/input/mime-types.c"
)

LIB_TEST_BINARIES=(
    "test_strbuf.exe"
    "test_strview.exe"
    "test_variable_pool.exe"
    "test_num_stack.exe"
    "test_mime_detect.exe"
)

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

# Function to compile and run validator tests
run_validator_tests() {
    print_status "🔧 Compiling and running Validator tests..."
    
    # Check if lambda executable exists
    if [ ! -f "./lambda.exe" ]; then
        print_error "Lambda executable not found. Run 'make' first."
        return 1
    fi

    print_success "Lambda executable ready"

    # Compile with full validator integration
    COMPILE_CMD="gcc -std=c99 -Wall -Wextra -g \
        -Iinclude -Ilambda -Ilambda/validator \
        $CRITERION_FLAGS \
        $VALIDATOR_TEST_SOURCES \
        -o $VALIDATOR_TEST_BINARY"

    print_status "Compiling validator tests..."

    if $COMPILE_CMD 2>/dev/null; then
        print_success "Validator test suite compiled successfully"
    else
        print_error "Failed to compile validator test suite"
        print_error "Attempting compilation with detailed error output..."
        $COMPILE_CMD
        return 1
    fi

    # Run the comprehensive Criterion test suite
    print_status "🧪 Running validator tests..."
    echo ""

    # Set environment variables for test files
    export TEST_DIR_PATH="$PWD/test/lambda/validator"
    export LAMBDA_EXE_PATH="$PWD/lambda.exe"

    # Run tests with detailed output
    set +e  # Disable strict error handling for test execution
    VALIDATOR_TEST_OUTPUT=$(./"$VALIDATOR_TEST_BINARY" --verbose --tap 2>&1)
    VALIDATOR_TEST_EXIT_CODE=$?
    set -e  # Re-enable strict error handling

    echo "$VALIDATOR_TEST_OUTPUT"
    echo ""

    # Parse validator test results
    VALIDATOR_TOTAL_TESTS=$(echo "$VALIDATOR_TEST_OUTPUT" | grep -c "^ok " 2>/dev/null || echo "0")
    VALIDATOR_FAILED_TESTS=$(echo "$VALIDATOR_TEST_OUTPUT" | grep -c "^not ok " 2>/dev/null || echo "0")

    # Clean numeric values
    VALIDATOR_TOTAL_TESTS=$(echo "$VALIDATOR_TOTAL_TESTS" | tr -cd '0-9')
    VALIDATOR_FAILED_TESTS=$(echo "$VALIDATOR_FAILED_TESTS" | tr -cd '0-9')

    # Ensure defaults
    VALIDATOR_TOTAL_TESTS=${VALIDATOR_TOTAL_TESTS:-0}
    VALIDATOR_FAILED_TESTS=${VALIDATOR_FAILED_TESTS:-0}
    
    # Store for final summary
    VALIDATOR_PASSED_TESTS=$((VALIDATOR_TOTAL_TESTS - VALIDATOR_FAILED_TESTS))

    # Cleanup validator binary
    if [ -f "$VALIDATOR_TEST_BINARY" ]; then
        rm "$VALIDATOR_TEST_BINARY"
    fi

    return $VALIDATOR_FAILED_TESTS
}

# Function to compile and run library tests
run_library_tests() {
    print_status "🔧 Compiling and running Library tests..."
    
    local lib_failed_tests=0
    local lib_total_tests=0
    local lib_passed_tests=0
    
    # Initialize arrays to store individual test results
    LIB_TEST_RESULTS=()
    LIB_TEST_NAMES=()
    LIB_TEST_TOTALS=()
    LIB_TEST_PASSED=()
    LIB_TEST_FAILED=()
    
    # Arrays to store background job PIDs and temporary result files
    local job_pids=()
    local result_files=()
    
    # Create temporary directory for parallel execution results
    local temp_dir=$(mktemp -d)
    
    # Compile and start each library test in parallel
    for i in "${!LIB_TEST_SOURCES[@]}"; do
        local test_source="test/${LIB_TEST_SOURCES[$i]}"
        local test_binary="test/${LIB_TEST_BINARIES[$i]}"
        local test_deps="${LIB_TEST_DEPENDENCIES[$i]}"
        local test_name="${LIB_TEST_SOURCES[$i]%%.c}"  # Remove .c extension
        local result_file="$temp_dir/result_$i.txt"
        
        print_status "Compiling $test_source..."
        
        # Compile command with dependencies
        COMPILE_CMD="clang -o $test_binary $test_source $test_deps $CRITERION_FLAGS -fms-extensions"
        
        if $COMPILE_CMD 2>/dev/null; then
            print_success "Compiled $test_source successfully"
            
            # Start test execution in background
            print_status "Starting $test_binary in parallel..."
            (
                # Run the test and capture output
                set +e  # Disable strict error handling for test execution
                LIB_TEST_OUTPUT=$(./"$test_binary" --verbose --tap 2>&1)
                LIB_TEST_EXIT_CODE=$?
                set -e  # Re-enable strict error handling
                
                # Parse test results
                TEST_TOTAL=$(echo "$LIB_TEST_OUTPUT" | grep -c "^ok " 2>/dev/null || echo "0")
                TEST_FAILED=$(echo "$LIB_TEST_OUTPUT" | grep -c "^not ok " 2>/dev/null || echo "0")
                
                # Clean numeric values
                TEST_TOTAL=$(echo "$TEST_TOTAL" | tr -cd '0-9')
                TEST_FAILED=$(echo "$TEST_FAILED" | tr -cd '0-9')
                
                # Ensure defaults
                TEST_TOTAL=${TEST_TOTAL:-0}
                TEST_FAILED=${TEST_FAILED:-0}
                TEST_PASSED=$((TEST_TOTAL - TEST_FAILED))
                
                # Write results to temporary file
                echo "TEST_NAME:$test_name" > "$result_file"
                echo "TEST_TOTAL:$TEST_TOTAL" >> "$result_file"
                echo "TEST_PASSED:$TEST_PASSED" >> "$result_file"
                echo "TEST_FAILED:$TEST_FAILED" >> "$result_file"
                echo "TEST_OUTPUT_START" >> "$result_file"
                echo "$LIB_TEST_OUTPUT" >> "$result_file"
                echo "TEST_OUTPUT_END" >> "$result_file"
                
                # Cleanup test binary
                if [ -f "$test_binary" ]; then
                    rm "$test_binary"
                fi
                
                exit $TEST_FAILED
            ) &
            
            # Store background job PID and result file
            job_pids+=($!)
            result_files+=("$result_file")
            
        else
            print_error "Failed to compile $test_source"
            print_error "Attempting compilation with detailed error output..."
            $COMPILE_CMD
            lib_failed_tests=$((lib_failed_tests + 1))
            
            # Store failed compilation result
            LIB_TEST_NAMES+=("$test_name")
            LIB_TEST_TOTALS+=(0)
            LIB_TEST_PASSED+=(0)
            LIB_TEST_FAILED+=(1)
        fi
    done
    
    # Wait for all background jobs and collect results
    print_status "⏳ Waiting for parallel test execution to complete..."
    
    for i in "${!job_pids[@]}"; do
        local pid="${job_pids[$i]}"
        local result_file="${result_files[$i]}"
        
        # Wait for this specific job
        wait $pid
        local job_exit_code=$?
        
        # Read results from temporary file
        if [ -f "$result_file" ]; then
            local test_name=$(grep "^TEST_NAME:" "$result_file" | cut -d: -f2)
            local test_total=$(grep "^TEST_TOTAL:" "$result_file" | cut -d: -f2)
            local test_passed=$(grep "^TEST_PASSED:" "$result_file" | cut -d: -f2)
            local test_failed=$(grep "^TEST_FAILED:" "$result_file" | cut -d: -f2)
            
            # Extract and display test output
            local test_output=$(sed -n '/^TEST_OUTPUT_START$/,/^TEST_OUTPUT_END$/p' "$result_file" | sed '1d;$d')
            
            print_status "📋 Results for $test_name:"
            echo "$test_output"
            echo ""
            
            # Store individual test results
            LIB_TEST_NAMES+=("$test_name")
            LIB_TEST_TOTALS+=($test_total)
            LIB_TEST_PASSED+=($test_passed)
            LIB_TEST_FAILED+=($test_failed)
            
            lib_total_tests=$((lib_total_tests + test_total))
            lib_failed_tests=$((lib_failed_tests + test_failed))
        else
            print_error "Failed to read results for job $i"
            lib_failed_tests=$((lib_failed_tests + 1))
        fi
    done
    
    # Cleanup temporary directory
    rm -rf "$temp_dir"
    
    lib_passed_tests=$((lib_total_tests - lib_failed_tests))
    
    # Store totals for final summary
    LIB_TOTAL_TESTS=$lib_total_tests
    LIB_PASSED_TESTS=$lib_passed_tests
    LIB_FAILED_TESTS=$lib_failed_tests
    
    echo ""
    print_status "📊 Library Test Results Summary:"
    echo "   Total Tests: $lib_total_tests"
    echo "   Passed: $lib_passed_tests"
    echo "   Failed: $lib_failed_tests"
    
    return $lib_failed_tests
}

# Main execution
echo ""
print_status "🚀 Starting comprehensive test suite..."

# Find Criterion installation
find_criterion

# Initialize counters and tracking arrays
total_failed_tests=0
total_passed_tests=0
total_tests=0

# Arrays to track test suite execution order and results
TEST_SUITE_ORDER=()
TEST_SUITE_NAMES=()
TEST_SUITE_TOTALS=()
TEST_SUITE_PASSED=()
TEST_SUITE_FAILED=()
TEST_SUITE_STATUS=()

# Run library tests first
print_status "================================================"
print_status "                LIBRARY TESTS                  "
print_status "================================================"
if run_library_tests; then
    library_failed=0
else
    library_failed=$?
fi

# Record library test suite results
TEST_SUITE_ORDER+=("LIBRARY")
TEST_SUITE_NAMES+=("📚 Library Tests")
TEST_SUITE_TOTALS+=($LIB_TOTAL_TESTS)
TEST_SUITE_PASSED+=($LIB_PASSED_TESTS) 
TEST_SUITE_FAILED+=($LIB_FAILED_TESTS)
if [ $library_failed -eq 0 ]; then
    TEST_SUITE_STATUS+=("PASSED")
else
    TEST_SUITE_STATUS+=("FAILED")
fi

echo ""

# Run validator tests
print_status "================================================"
print_status "               VALIDATOR TESTS                  "
print_status "================================================"
if run_validator_tests; then
    validator_failed=0
else
    validator_failed=$?
fi

# Record validator test suite results
TEST_SUITE_ORDER+=("VALIDATOR")
TEST_SUITE_NAMES+=("🔍 Validator Tests")
TEST_SUITE_TOTALS+=($VALIDATOR_TOTAL_TESTS)
TEST_SUITE_PASSED+=($VALIDATOR_PASSED_TESTS)
TEST_SUITE_FAILED+=($VALIDATOR_FAILED_TESTS)
if [ $validator_failed -eq 0 ]; then
    TEST_SUITE_STATUS+=("PASSED")
else
    TEST_SUITE_STATUS+=("FAILED")
fi

# Calculate totals dynamically from all test suites
total_tests_run=0
total_passed_tests=0
total_failed_tests=0

for i in "${!TEST_SUITE_TOTALS[@]}"; do
    total_tests_run=$((total_tests_run + TEST_SUITE_TOTALS[$i]))
    total_passed_tests=$((total_passed_tests + TEST_SUITE_PASSED[$i]))
    total_failed_tests=$((total_failed_tests + TEST_SUITE_FAILED[$i]))
done

echo ""
print_status "================================================"
print_status "              FINAL TEST SUMMARY               "
print_status "================================================"

if [ "$total_failed_tests" -eq 0 ]; then
    print_success "🎉 ALL TESTS PASSED!"
    echo ""
    print_success "✨ Lambda project is ready for production use!"
    echo ""
    print_status "📊 Detailed Test Results:"
    echo ""
    
    # Dynamic test suite breakdown based on execution order
    for i in "${!TEST_SUITE_ORDER[@]}"; do
        suite_type="${TEST_SUITE_ORDER[$i]}"
        suite_name="${TEST_SUITE_NAMES[$i]}"
        suite_total="${TEST_SUITE_TOTALS[$i]}"
        suite_passed="${TEST_SUITE_PASSED[$i]}"
        suite_failed="${TEST_SUITE_FAILED[$i]}"
        
        print_status "$suite_name:"
        
        # Show detailed breakdown for library tests
        if [ "$suite_type" = "LIBRARY" ]; then
            for j in "${!LIB_TEST_NAMES[@]}"; do
                test_name="${LIB_TEST_NAMES[$j]}"
                test_total="${LIB_TEST_TOTALS[$j]}"
                test_passed="${LIB_TEST_PASSED[$j]}"
                
                echo "   ├─ $test_name: $test_total tests (✅ $test_passed passed)"
            done
        fi
        
        echo "   └─ Total: $suite_total, Passed: $suite_passed, Failed: $suite_failed"
        echo ""
    done
    
    print_status "🎯 Overall Summary:"
    echo "   Total Test Suites: ${#TEST_SUITE_ORDER[@]}"
    echo "   Total Tests: $total_tests_run"
    echo "   Total Passed: $total_passed_tests"
    echo "   Total Failed: 0"
    
    exit 0
else
    print_warning "⚠️  Some tests failed"
    echo ""
    print_status "📊 Detailed Test Results:"
    echo ""
    
    # Dynamic test suite breakdown based on execution order
    for i in "${!TEST_SUITE_ORDER[@]}"; do
        suite_type="${TEST_SUITE_ORDER[$i]}"
        suite_name="${TEST_SUITE_NAMES[$i]}"
        suite_total="${TEST_SUITE_TOTALS[$i]}"
        suite_passed="${TEST_SUITE_PASSED[$i]}"
        suite_failed="${TEST_SUITE_FAILED[$i]}"
        suite_status="${TEST_SUITE_STATUS[$i]}"
        
        print_status "$suite_name:"
        
        # Show detailed breakdown for library tests
        if [ "$suite_type" = "LIBRARY" ]; then
            for j in "${!LIB_TEST_NAMES[@]}"; do
                test_name="${LIB_TEST_NAMES[$j]}"
                test_total="${LIB_TEST_TOTALS[$j]}"
                test_passed="${LIB_TEST_PASSED[$j]}"
                test_failed="${LIB_TEST_FAILED[$j]}"
                
                if [ "$test_failed" -eq 0 ]; then
                    echo "   ├─ $test_name: $test_total tests (✅ $test_passed passed) ✅"
                else
                    echo "   ├─ $test_name: $test_total tests (✅ $test_passed passed, ❌ $test_failed failed) ❌"
                fi
            done
        fi
        
        # Add status indicator for overall suite
        if [ "$suite_status" = "PASSED" ]; then
            echo "   └─ Total: $suite_total, Passed: $suite_passed, Failed: $suite_failed ✅"
        else
            echo "   └─ Total: $suite_total, Passed: $suite_passed, Failed: $suite_failed ❌"
        fi
        echo ""
    done
    
    print_status "🎯 Overall Summary:"
    echo "   Total Test Suites: ${#TEST_SUITE_ORDER[@]}"
    echo "   Total Tests: $total_tests_run"
    echo "   Total Passed: $total_passed_tests"
    echo "   Total Failed: $total_failed_tests"
    echo ""
    print_status "💡 Breakdown by Suite:"
    
    # Dynamic breakdown by suite
    for i in "${!TEST_SUITE_ORDER[@]}"; do
        suite_type="${TEST_SUITE_ORDER[$i]}"
        suite_failed="${TEST_SUITE_FAILED[$i]}"
        
        if [ "$suite_type" = "LIBRARY" ]; then
            echo "   Library test failures: $suite_failed"
        elif [ "$suite_type" = "VALIDATOR" ]; then
            echo "   Validator test failures: $suite_failed"
        fi
    done
    
    echo ""
    print_warning "⚠️  Review failed tests above for details"
    print_status "📋 See lambda/validator/validator.md for comprehensive test coverage information" 
    echo ""
    exit 1
fi
