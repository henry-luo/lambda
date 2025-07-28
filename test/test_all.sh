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

# MIR Test Configuration
MIR_TEST_SOURCES="test/test_mir.c"
MIR_TEST_BINARY="test/test_mir.exe"

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

# Function to detect CPU cores
detect_cpu_cores() {
    local cores=1  # Default fallback
    
    # Try multiple methods to detect CPU cores
    if command -v nproc >/dev/null 2>&1; then
        cores=$(nproc)
        print_status "Detected $cores CPU cores using nproc"
    elif command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu >/dev/null 2>&1; then
        cores=$(sysctl -n hw.ncpu)
        print_status "Detected $cores CPU cores using sysctl (macOS)"
    elif command -v getconf >/dev/null 2>&1; then
        cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo "1")
        print_status "Detected $cores CPU cores using getconf"
    elif [ -r /proc/cpuinfo ]; then
        cores=$(grep -c ^processor /proc/cpuinfo 2>/dev/null || echo "1")
        print_status "Detected $cores CPU cores using /proc/cpuinfo"
    else
        print_warning "Could not detect CPU cores, using default of 1"
    fi
    
    # Ensure we have a valid number
    if ! [[ "$cores" =~ ^[0-9]+$ ]] || [ "$cores" -lt 1 ]; then
        print_warning "Invalid core count detected ($cores), using fallback of 1"
        cores=1
    fi
    
    # For very high core counts, cap at 16 to avoid overwhelming the system
    if [ "$cores" -gt 16 ]; then
        print_status "Capping jobs at 16 (detected $cores cores)"
        cores=16
    fi
    
    CPU_CORES=$cores
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
    VALIDATOR_TEST_OUTPUT=$(./"$VALIDATOR_TEST_BINARY" --verbose --tap --jobs=$CPU_CORES 2>&1)
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

# Function to compile and run MIR JIT tests
run_mir_tests() {
    print_status "🔧 Compiling and running MIR JIT tests..."
    
    # Check if jq is available
    if ! command -v jq &> /dev/null; then
        print_error "jq is required to parse the configuration file"
        print_error "Please install jq: brew install jq"
        return 1
    fi

    # Get the project root and config file
    CONFIG_FILE="build_lambda_config.json"

    if [ ! -f "$CONFIG_FILE" ]; then
        print_error "Configuration file $CONFIG_FILE not found!"
        return 1
    fi

    print_status "Loading object files from $CONFIG_FILE..."

    # Extract source files from JSON and convert to object file paths
    OBJECT_FILES=()

    # Process source_files
    while IFS= read -r source_file; do
        # Convert source file path to object file path
        # Remove leading directory path and change extension to .o
        obj_file="build/$(basename "$source_file" .c).o"
        if [ -f "$obj_file" ]; then
            OBJECT_FILES+=("$PWD/$obj_file")
        else
            print_warning "Object file $obj_file not found. You may need to build the project first."
        fi
    done < <(jq -r '.source_files[]' "$CONFIG_FILE" | grep -E '\.(c|cpp)$')

    # Process source_dirs - find all .c files in the directories
    SOURCE_DIRS=$(jq -r '.source_dirs[]?' "$CONFIG_FILE" 2>/dev/null)
    if [ -n "$SOURCE_DIRS" ]; then
        while IFS= read -r source_dir; do
            if [ -d "$source_dir" ]; then
                while IFS= read -r source_file; do
                    # Convert absolute path to relative path from project root
                    rel_path="${source_file#$PWD/}"
                    obj_file="build/$(basename "$rel_path" .c).o"
                    if [ -f "$obj_file" ]; then
                        OBJECT_FILES+=("$PWD/$obj_file")
                    else
                        print_warning "Object file $obj_file not found for $rel_path. You may need to build the project first."
                    fi
                done < <(find "$source_dir" -name "*.c" -type f)
            fi
        done <<< "$SOURCE_DIRS"
    fi

    print_status "Found ${#OBJECT_FILES[@]} object files"

    # Check if we have any object files
    if [ ${#OBJECT_FILES[@]} -eq 0 ]; then
        print_error "No object files found. Please build the project first using: ./compile.sh"
        return 1
    fi

    # Extract compiler flags from JSON
    COMPILER_FLAGS=$(jq -r '.compiler_flags // []' "$CONFIG_FILE")
    LIBRARIES=$(jq -r '.libraries // []' "$CONFIG_FILE")

    # Default compiler flags for MIR testing
    DEFAULT_FLAGS="-std=c99 -Wall -Wextra -O2 -g -fms-extensions -pedantic"

    # Build include directory flags from libraries
    INCLUDE_FLAGS=""
    STATIC_LIBS=""
    DYNAMIC_LIBS=""

    if [ "$LIBRARIES" != "null" ] && [ "$LIBRARIES" != "[]" ]; then
        while IFS= read -r lib_info; do
            name=$(echo "$lib_info" | jq -r '.name')
            include=$(echo "$lib_info" | jq -r '.include // empty')
            lib=$(echo "$lib_info" | jq -r '.lib // empty')
            link=$(echo "$lib_info" | jq -r '.link // "static"')
            
            # Add include directory if specified
            if [ -n "$include" ] && [ "$include" != "null" ]; then
                INCLUDE_FLAGS="$INCLUDE_FLAGS -I$include"
            fi
            
            # Add library based on link type
            if [ "$link" = "static" ]; then
                if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                    STATIC_LIBS="$STATIC_LIBS $lib"
                fi
            else
                if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                    DYNAMIC_LIBS="$DYNAMIC_LIBS -L$lib -l$name"
                fi
            fi
        done < <(echo "$LIBRARIES" | jq -c '.[]')
    fi

    # Compile the MIR test
    print_status "Compiling MIR test..."
    COMPILE_CMD="gcc $DEFAULT_FLAGS $INCLUDE_FLAGS $CRITERION_FLAGS -o $MIR_TEST_BINARY $MIR_TEST_SOURCES ${OBJECT_FILES[*]} $STATIC_LIBS $DYNAMIC_LIBS"

    if $COMPILE_CMD 2>/dev/null; then
        print_success "MIR test suite compiled successfully"
    else
        print_error "Failed to compile MIR test suite"
        print_error "Attempting compilation with detailed error output..."
        $COMPILE_CMD
        return 1
    fi

    # Run the MIR tests
    print_status "🧪 Running MIR JIT tests..."
    echo ""

    # Run tests with detailed output
    set +e  # Disable strict error handling for test execution
    MIR_TEST_OUTPUT=$(./"$MIR_TEST_BINARY" --verbose --tap 2>&1)
    MIR_TEST_EXIT_CODE=$?
    set -e  # Re-enable strict error handling

    echo "$MIR_TEST_OUTPUT"
    echo ""

    # Parse MIR test results
    MIR_TOTAL_TESTS=$(echo "$MIR_TEST_OUTPUT" | grep -c "^ok " 2>/dev/null || echo "0")
    MIR_FAILED_TESTS=$(echo "$MIR_TEST_OUTPUT" | grep -c "^not ok " 2>/dev/null || echo "0")

    # Clean numeric values
    MIR_TOTAL_TESTS=$(echo "$MIR_TOTAL_TESTS" | tr -cd '0-9')
    MIR_FAILED_TESTS=$(echo "$MIR_FAILED_TESTS" | tr -cd '0-9')

    # Ensure defaults
    MIR_TOTAL_TESTS=${MIR_TOTAL_TESTS:-0}
    MIR_FAILED_TESTS=${MIR_FAILED_TESTS:-0}
    
    # Store for final summary
    MIR_PASSED_TESTS=$((MIR_TOTAL_TESTS - MIR_FAILED_TESTS))

    # Cleanup MIR binary
    if [ -f "$MIR_TEST_BINARY" ]; then
        rm "$MIR_TEST_BINARY"
    fi

    return $MIR_FAILED_TESTS
}

# Main execution
echo ""
print_status "🚀 Starting comprehensive test suite..."

# Detect CPU cores for parallel execution
detect_cpu_cores

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

# Create temporary directory for parallel test suite execution
SUITE_TEMP_DIR=$(mktemp -d)

# Arrays to store test suite background job PIDs and result files
SUITE_JOB_PIDS=()
SUITE_RESULT_FILES=()
SUITE_TYPES=()

print_status "🚀 Starting all test suites in parallel..."
echo ""

# Temporarily disable strict error handling for parallel execution
set +e

# Start Library tests in background
print_status "================================================"
print_status "        STARTING LIBRARY TESTS (PARALLEL)      "
print_status "================================================"
LIBRARY_RESULT_FILE="$SUITE_TEMP_DIR/library_results.txt"
(
    # Run library tests and capture results
    print_status "📚 Library Tests - Starting..."
    if run_library_tests; then
        library_failed=0
    else
        library_failed=$?
    fi
    
    # Write results to file (variables are available in this subshell)
    echo "SUITE_TYPE:LIBRARY" > "$LIBRARY_RESULT_FILE"
    echo "SUITE_NAME:📚 Library Tests" >> "$LIBRARY_RESULT_FILE"
    echo "SUITE_TOTAL:$LIB_TOTAL_TESTS" >> "$LIBRARY_RESULT_FILE"
    echo "SUITE_PASSED:$LIB_PASSED_TESTS" >> "$LIBRARY_RESULT_FILE"
    echo "SUITE_FAILED:$LIB_FAILED_TESTS" >> "$LIBRARY_RESULT_FILE"
    if [ $library_failed -eq 0 ]; then
        echo "SUITE_STATUS:PASSED" >> "$LIBRARY_RESULT_FILE"
    else
        echo "SUITE_STATUS:FAILED" >> "$LIBRARY_RESULT_FILE"
    fi
    
    # Output with prefix for identification
    echo "[LIBRARY] Library tests completed with exit code: $library_failed"
    exit $library_failed
) &
SUITE_JOB_PIDS+=($!)
SUITE_RESULT_FILES+=("$LIBRARY_RESULT_FILE")
SUITE_TYPES+=("LIBRARY")

# Start MIR JIT tests in background
print_status "================================================"
print_status "       STARTING MIR JIT TESTS (PARALLEL)       "
print_status "================================================"
MIR_RESULT_FILE="$SUITE_TEMP_DIR/mir_results.txt"
(
    # Run MIR tests and capture results
    print_status "⚡ MIR JIT Tests - Starting..."
    if run_mir_tests; then
        mir_failed=0
    else
        mir_failed=$?
    fi
    
    # Write results to file (variables are available in this subshell)
    echo "SUITE_TYPE:MIR" > "$MIR_RESULT_FILE"
    echo "SUITE_NAME:⚡ MIR JIT Tests" >> "$MIR_RESULT_FILE"
    echo "SUITE_TOTAL:$MIR_TOTAL_TESTS" >> "$MIR_RESULT_FILE"
    echo "SUITE_PASSED:$MIR_PASSED_TESTS" >> "$MIR_RESULT_FILE"
    echo "SUITE_FAILED:$MIR_FAILED_TESTS" >> "$MIR_RESULT_FILE"
    if [ $mir_failed -eq 0 ]; then
        echo "SUITE_STATUS:PASSED" >> "$MIR_RESULT_FILE"
    else
        echo "SUITE_STATUS:FAILED" >> "$MIR_RESULT_FILE"
    fi
    
    # Output with prefix for identification
    echo "[MIR] MIR JIT tests completed with exit code: $mir_failed"
    exit $mir_failed
) &
SUITE_JOB_PIDS+=($!)
SUITE_RESULT_FILES+=("$MIR_RESULT_FILE")
SUITE_TYPES+=("MIR")

# Start Validator tests in background
print_status "================================================"
print_status "      STARTING VALIDATOR TESTS (PARALLEL)      "
print_status "================================================"
VALIDATOR_RESULT_FILE="$SUITE_TEMP_DIR/validator_results.txt"
(
    # Run validator tests and capture results
    print_status "🔍 Validator Tests - Starting..."
    if run_validator_tests; then
        validator_failed=0
    else
        validator_failed=$?
    fi
    
    # Write results to file (variables are available in this subshell)
    echo "SUITE_TYPE:VALIDATOR" > "$VALIDATOR_RESULT_FILE"
    echo "SUITE_NAME:🔍 Validator Tests" >> "$VALIDATOR_RESULT_FILE"
    echo "SUITE_TOTAL:$VALIDATOR_TOTAL_TESTS" >> "$VALIDATOR_RESULT_FILE"
    echo "SUITE_PASSED:$VALIDATOR_PASSED_TESTS" >> "$VALIDATOR_RESULT_FILE"
    echo "SUITE_FAILED:$VALIDATOR_FAILED_TESTS" >> "$VALIDATOR_RESULT_FILE"
    if [ $validator_failed -eq 0 ]; then
        echo "SUITE_STATUS:PASSED" >> "$VALIDATOR_RESULT_FILE"
    else
        echo "SUITE_STATUS:FAILED" >> "$VALIDATOR_RESULT_FILE"
    fi
    
    # Output with prefix for identification
    echo "[VALIDATOR] Validator tests completed with exit code: $validator_failed"
    exit $validator_failed
) &
SUITE_JOB_PIDS+=($!)
SUITE_RESULT_FILES+=("$VALIDATOR_RESULT_FILE")
SUITE_TYPES+=("VALIDATOR")

# Re-enable strict error handling now that all background jobs are started
set -e

echo ""
print_status "⏳ Waiting for all test suites to complete..."
print_status "📊 Started ${#SUITE_JOB_PIDS[@]} test suites with PIDs: ${SUITE_JOB_PIDS[*]}"

# Wait for all test suites and collect results
for i in 0 1 2; do
    pid="${SUITE_JOB_PIDS[$i]}"
    result_file="${SUITE_RESULT_FILES[$i]}"
    suite_type="${SUITE_TYPES[$i]}"
    
    print_status "⏳ Waiting for $suite_type test suite (PID: $pid)..."
    
    # Simple wait with timeout using background monitoring
    (
        sleep 300  # 5 minute timeout
        if kill -0 "$pid" 2>/dev/null; then
            echo "TIMEOUT: $suite_type test suite (PID: $pid) timed out after 5 minutes"
            kill -9 "$pid" 2>/dev/null || true
        fi
    ) &
    timeout_pid=$!
    
    # Wait for the actual test suite process
    wait "$pid" 2>/dev/null
    suite_exit_code=$?
    
    # Kill the timeout monitor since the process completed
    kill "$timeout_pid" 2>/dev/null || true
    wait "$timeout_pid" 2>/dev/null || true
    
    print_status "📋 $suite_type completed with exit code: $suite_exit_code"
    
    # Read results from file
    if [ -f "$result_file" ]; then
        suite_type_read=$(grep "^SUITE_TYPE:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "$suite_type")
        suite_name=$(grep "^SUITE_NAME:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "❌ $suite_type Tests")
        suite_total=$(grep "^SUITE_TOTAL:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "0")
        suite_passed=$(grep "^SUITE_PASSED:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "0")
        suite_failed=$(grep "^SUITE_FAILED:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "1")
        suite_status=$(grep "^SUITE_STATUS:" "$result_file" 2>/dev/null | cut -d: -f2 || echo "FAILED")
        
        # Debug: Show what we read from the file
        print_status "📊 Results for $suite_type: Total=$suite_total, Passed=$suite_passed, Failed=$suite_failed, Status=$suite_status"
        
        # Store results in arrays
        TEST_SUITE_ORDER+=("$suite_type_read")
        TEST_SUITE_NAMES+=("$suite_name")
        TEST_SUITE_TOTALS+=($suite_total)
        TEST_SUITE_PASSED+=($suite_passed)
        TEST_SUITE_FAILED+=($suite_failed)
        TEST_SUITE_STATUS+=("$suite_status")
        
        print_success "✅ $suite_name completed"
    else
        print_error "❌ Failed to read results for $suite_type test suite (result file not found)"
        # Add default failed entry
        TEST_SUITE_ORDER+=("$suite_type")
        TEST_SUITE_NAMES+=("❌ $suite_type Tests (Failed)")
        TEST_SUITE_TOTALS+=(0)
        TEST_SUITE_PASSED+=(0)
        TEST_SUITE_FAILED+=(1)
        TEST_SUITE_STATUS+=("FAILED")
    fi
done

# Cleanup temporary directory
rm -rf "$SUITE_TEMP_DIR"

echo ""
print_success "🎉 All test suites completed!"

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
        elif [ "$suite_type" = "MIR" ]; then
            echo "   ├─ MIR JIT Tests: $suite_total tests (✅ $suite_passed passed)"
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
        elif [ "$suite_type" = "MIR" ]; then
            if [ "$suite_failed" -eq 0 ]; then
                echo "   ├─ MIR JIT Tests: $suite_total tests (✅ $suite_passed passed) ✅"
            else
                echo "   ├─ MIR JIT Tests: $suite_total tests (✅ $suite_passed passed, ❌ $suite_failed failed) ❌"
            fi
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
        elif [ "$suite_type" = "MIR" ]; then
            echo "   MIR JIT test failures: $suite_failed"
        fi
    done
    
    echo ""
    print_warning "⚠️  Review failed tests above for details"
    print_status "📋 See lambda/validator/validator.md for comprehensive test coverage information" 
    echo ""
    exit 1
fi
