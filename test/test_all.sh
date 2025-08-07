#!/usr/bin/env bash

# Lambda Comprehensive Test Suite Runner
# Compiles and runs all tests including validator and library tests as proper Criterion unit tests

set -e  # Exit on any error

# Parse command line arguments
TARGET_TEST=""
SHOW_HELP=false
RAW_OUTPUT=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --target=*)
            TARGET_TEST="${arg#*=}"
            shift
            ;;
        --raw)
            RAW_OUTPUT=true
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
    echo "Lambda Comprehensive Test Suite Runner"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --target=TEST     Run a specific test suite"
    echo "  --raw             Run test executable directly without shell wrapper"
    echo "  --help, -h        Show this help message"
    echo ""
    echo "Available test targets:"
    echo "  all               Run all test suites (default)"
    echo "  library           Run only library tests"
    echo "  input             Run only input processing tests"
    echo "  validator         Run only validator tests"
    echo "  mir               Run only MIR JIT tests"
    echo "  lambda            Run only lambda runtime tests"
    echo "  strbuf            Run only string buffer tests"
    echo "  strview           Run only string view tests"
    echo "  variable_pool     Run only variable pool tests"
    echo "  num_stack         Run only number stack tests"
    echo "  mime_detect       Run only MIME detection tests"
    echo "  math              Run only math roundtrip tests"
    echo ""
    echo "Configuration:"
    echo "  Test configurations are loaded from test/test_config.json"
    echo "  This file defines test suites, sources, dependencies, and compilation flags"
    echo ""
    echo "Examples:"
    echo "  $0                      # Run all tests"
    echo "  $0 --target=math        # Run only math tests"
    echo "  $0 --target=library     # Run all library tests"
    echo "  $0 --target=input --raw # Run input tests with raw output"
    echo ""
    exit 0
fi

echo "================================================"
echo "     Lambda Comprehensive Test Suite Runner    "
echo "================================================"

# Display target information
if [ -n "$TARGET_TEST" ]; then
    echo "🎯 Target: Running '$TARGET_TEST' tests only"
else
    echo "🎯 Target: Running all test suites"
    TARGET_TEST="all"
fi
echo ""

# Configuration file path
TEST_CONFIG_FILE="test/test_config.json"

# Global variables for loaded test configuration
declare -a TEST_SUITE_NAMES
declare -a SUITE_CONFIGS

# Helper function to get configuration value
get_config() {
    local suite="$1"
    local key="$2"
    
    if [ ! -f "$TEST_CONFIG_FILE" ]; then
        echo ""
        return 1
    fi
    
    jq -r ".tests[] | select(.suite == \"$suite\") | .$key // \"\"" "$TEST_CONFIG_FILE" 2>/dev/null || echo ""
}

# Helper function to get configuration array as string
get_config_array() {
    local suite="$1"
    local key="$2"
    local separator="${3:-|||}"
    
    if [ ! -f "$TEST_CONFIG_FILE" ]; then
        echo ""
        return 1
    fi
    
    jq -r ".tests[] | select(.suite == \"$suite\") | .$key | join(\"$separator\")" "$TEST_CONFIG_FILE" 2>/dev/null || echo ""
}

# Helper function to parse delimiter-separated string into array (bash 3.2 compatible)
parse_array_string() {
    local input_str="$1"
    local delimiter="$2"
    local -a result_array=()
    
    local temp_str="$input_str"
    
    while true; do
        if [[ "$temp_str" == *"$delimiter"* ]]; then
            # Extract first part before delimiter
            local first_part="${temp_str%%$delimiter*}"
            result_array+=("$first_part")
            # Remove first part and delimiter
            temp_str="${temp_str#*$delimiter}"
        else
            # Last part (no more delimiters)
            result_array+=("$temp_str")
            break
        fi
    done
    
    # Print the array elements (this function is used in command substitution)
    printf "%s\n" "${result_array[@]}"
}

# Helper function to get build configuration file path
get_build_config_file() {
    local suite_name="$1"
    local config_file=$(get_config "$suite_name" "build_config_file")
    if [ -n "$config_file" ] && [ "$config_file" != "null" ]; then
        echo "$config_file"
    else
        echo "build_lambda_config.json"
    fi
}

# Function to run test executable with raw output (no shell wrapper)
run_raw_test() {
    local test_binary="$1"
    local test_name="$2"
    
    if [ ! -f "$test_binary" ]; then
        echo "❌ Test binary $test_binary not found"
        return 1
    fi
    
    # Run the test directly and let it handle its own output
    exec ./"$test_binary"
}

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

# Function to load test configuration from JSON
load_test_config() {
    if [ ! -f "$TEST_CONFIG_FILE" ]; then
        print_error "Test configuration file $TEST_CONFIG_FILE not found!"
        exit 1
    fi
    
    if ! command -v jq &> /dev/null; then
        print_error "jq is required to parse the configuration file"
        print_error "Please install jq: brew install jq"
        exit 1
    fi
    
    print_status "Loading test configuration from $TEST_CONFIG_FILE..."
    
    # Load all test suite names (compatible with bash 3.2+)
    TEST_SUITE_NAMES=()
    while IFS= read -r suite_name; do
        TEST_SUITE_NAMES+=("$suite_name")
    done < <(jq -r '.tests[].suite' "$TEST_CONFIG_FILE")
    
    print_success "Loaded ${#TEST_SUITE_NAMES[@]} test suite configurations"
}

# Generic function to store test suite results
store_suite_results() {
    local suite_name="$1"
    local total_tests="$2"
    local passed_tests="$3"
    local failed_tests="$4"
    local test_names_arr_name="$5"
    local test_totals_arr_name="$6"
    local test_passed_arr_name="$7"
    local test_failed_arr_name="$8"
    
    # Convert suite name to uppercase for variable naming
    local suite_upper=$(echo "$suite_name" | tr '[:lower:]' '[:upper:]')
    
    # Store test counts using eval for compatibility
    eval "${suite_upper}_TOTAL_TESTS=$total_tests"
    eval "${suite_upper}_PASSED_TESTS=$passed_tests"
    eval "${suite_upper}_FAILED_TESTS=$failed_tests"
    
    # Copy arrays using eval for compatibility
    eval "${suite_upper}_TEST_NAMES=(\"\${${test_names_arr_name}[@]}\")"
    eval "${suite_upper}_TEST_TOTALS=(\"\${${test_totals_arr_name}[@]}\")"
    eval "${suite_upper}_TEST_PASSED=(\"\${${test_passed_arr_name}[@]}\")"
    eval "${suite_upper}_TEST_FAILED=(\"\${${test_failed_arr_name}[@]}\")"
    
    # Also create legacy aliases for backward compatibility
    # Define legacy alias mappings (suite_name -> legacy_prefix)
    local legacy_prefix=""
    case "$suite_name" in
        "library") legacy_prefix="LIB" ;;
        "input") legacy_prefix="INPUT" ;;
        "mir") legacy_prefix="MIR" ;;
        "lambda") legacy_prefix="LAMBDA" ;;
        *) legacy_prefix="" ;;  # No legacy aliases for other suites
    esac
    
    # Create legacy aliases if a mapping exists
    if [ -n "$legacy_prefix" ]; then
        eval "${legacy_prefix}_TOTAL_TESTS=$total_tests"
        eval "${legacy_prefix}_PASSED_TESTS=$passed_tests"
        eval "${legacy_prefix}_FAILED_TESTS=$failed_tests"
        eval "${legacy_prefix}_TEST_NAMES=(\"\${${test_names_arr_name}[@]}\")"
        eval "${legacy_prefix}_TEST_TOTALS=(\"\${${test_totals_arr_name}[@]}\")"
        eval "${legacy_prefix}_TEST_PASSED=(\"\${${test_passed_arr_name}[@]}\")"
        eval "${legacy_prefix}_TEST_FAILED=(\"\${${test_failed_arr_name}[@]}\")"
    fi
}

# Generic function to get test suite results
get_suite_results() {
    local suite_name="$1"
    local result_type="$2"  # "total", "passed", "failed", "names", "totals", "test_passed", "test_failed"
    
    # Convert suite name to uppercase for variable naming
    local suite_upper=$(echo "$suite_name" | tr '[:lower:]' '[:upper:]')
    local var_name="${suite_upper}_"
    
    case "$result_type" in
        "total")
            var_name="${var_name}TOTAL_TESTS"
            ;;
        "passed")
            var_name="${var_name}PASSED_TESTS"
            ;;
        "failed")
            var_name="${var_name}FAILED_TESTS"
            ;;
        "names")
            var_name="${var_name}TEST_NAMES"
            ;;
        "totals")
            var_name="${var_name}TEST_TOTALS"
            ;;
        "test_passed")
            var_name="${var_name}TEST_PASSED"
            ;;
        "test_failed")
            var_name="${var_name}TEST_FAILED"
            ;;
        *)
            echo ""
            return 1
            ;;
    esac
    
    # Return the value of the dynamic variable
    eval "echo \"\${$var_name}\""
}

# Function to display suite results summary using the generic approach
display_suite_summary() {
    local suite_name="$1"
    local suite_display_name=$(get_config "$suite_name" "name")
    
    local total=$(get_suite_results "$suite_name" "total")
    local passed=$(get_suite_results "$suite_name" "passed")
    local failed=$(get_suite_results "$suite_name" "failed")
    
    if [ -n "$total" ] && [ "$total" -gt 0 ]; then
        print_status "📊 $suite_display_name Summary (via generic API):"
        echo "   Total: $total, Passed: $passed, Failed: $failed"
        if [ "$failed" -eq 0 ]; then
            print_success "All $suite_name tests passed!"
        else
            print_error "$failed $suite_name test(s) failed"
        fi
    fi
}

# Common function to run any test suite based on configuration
run_common_test_suite() {
    local suite_name="$1"
    local is_parallel="${2:-false}"
    
    local suite_display_name=$(get_config "$suite_name" "name")
    print_status "🔧 Compiling and running $suite_display_name..."
    
    # Check prerequisites
    local requires_lambda_exe=$(get_config "$suite_name" "requires_lambda_exe")
    if [ "$requires_lambda_exe" = "true" ]; then
        if [ ! -f "./lambda.exe" ]; then
            print_error "Lambda executable not found. Run 'make' first."
            return 1
        fi
        print_success "Lambda executable ready"
    fi
    
    local requires_jq=$(get_config "$suite_name" "requires_jq")
    if [ "$requires_jq" = "true" ]; then
        if ! command -v jq &> /dev/null; then
            print_error "jq is required for ${suite_name} tests"
            print_error "Please install jq: brew install jq"
            return 1
        fi
    fi
    
    # Set environment variables if specified
    local env_vars=$(jq -r ".tests[] | select(.suite == \"$suite_name\") | .environment // {} | to_entries | map(\"\(.key)=\(.value)\") | join(\"|||\")" "$TEST_CONFIG_FILE" 2>/dev/null || echo "")
    if [ -n "$env_vars" ] && [ "$env_vars" != "null" ]; then
        IFS='|||' read -ra ENV_ARRAY <<< "$env_vars"
        for env_var in "${ENV_ARRAY[@]}"; do
            if [ -n "$env_var" ]; then
                # Expand variables in the environment value
                expanded_env_var=$(eval echo "$env_var")
                export "$expanded_env_var"
                print_status "Set environment: $expanded_env_var"
            fi
        done
    fi
    
    # Handle different test types based on configuration
    local suite_type=$(get_config "$suite_name" "type")
    local is_parallel_suite=$(get_config "$suite_name" "parallel")
    
    case "$suite_type" in
        "validator")
            run_validator_suite_impl "$suite_name"
            return $?
            ;;
        "library"|"input"|"mir"|"lambda")
            # All these suite types now use the unified parallel implementation
            if [ "$is_parallel" = "true" ] || [ "$is_parallel_suite" = "true" ] || [ "$suite_type" = "mir" ] || [ "$suite_type" = "lambda" ]; then
                run_parallel_suite_impl "$suite_name"
                return $?
            else
                run_sequential_suite_impl "$suite_name"
                return $?
            fi
            ;;
        *)
            print_error "Unknown test type: $suite_type"
            return 1
            ;;
    esac
}

# Implementation for validator test suite
run_validator_suite_impl() {
    local suite_name="$1"
    local sources=$(get_config_array "$suite_name" "sources" " ")
    local binary=$(get_config_array "$suite_name" "binaries" " ")
    local special_flags=$(get_config "$suite_name" "special_flags")
    
    # Compile validator tests
    local compile_cmd="gcc -std=c99 -Wall -Wextra -g $special_flags $CRITERION_FLAGS $sources -o $binary"
    
    print_status "Compiling validator tests..."
    if $compile_cmd 2>/dev/null; then
        print_success "Validator test suite compiled successfully"
    else
        print_error "Failed to compile validator test suite"
        print_error "Attempting compilation with detailed error output..."
        $compile_cmd
        return 1
    fi
    
    # Run validator tests
    print_status "🧪 Running validator tests..."
    echo ""
    
    set +e
    local test_output=$(./"$binary" --verbose --tap --jobs=$CPU_CORES 2>&1)
    local test_exit_code=$?
    set -e
    
    echo "$test_output"
    echo ""
    
    # Parse results
    local total_tests=$(echo "$test_output" | grep -c "^ok " 2>/dev/null || echo "0")
    local failed_tests=$(echo "$test_output" | grep -c "^not ok " 2>/dev/null || echo "0")
    
    # Clean numeric values
    total_tests=$(echo "$total_tests" | tr -cd '0-9')
    failed_tests=$(echo "$failed_tests" | tr -cd '0-9')
    total_tests=${total_tests:-0}
    failed_tests=${failed_tests:-0}
    
    # Store for final summary
    VALIDATOR_TOTAL_TESTS=$total_tests
    VALIDATOR_FAILED_TESTS=$failed_tests
    VALIDATOR_PASSED_TESTS=$((total_tests - failed_tests))
    
    # Cleanup
    if [ -f "$binary" ]; then
        rm "$binary"
    fi
    
    return $failed_tests
}

# Implementation for Lambda test suite using parallel execution
run_lambda_suite_impl() {
    local suite_name="$1"
    # Simply delegate to the parallel suite implementation
    run_parallel_suite_impl "$suite_name"
    return $?
}

# Implementation for MIR test suite using parallel execution
run_mir_suite_impl() {
    local suite_name="$1"
    # Simply delegate to the parallel suite implementation
    run_parallel_suite_impl "$suite_name"
    return $?
}

# Implementation for parallel test suites (library, input, mir, and lambda)
run_parallel_suite_impl() {
    local suite_name="$1"
    local sources_str=$(get_config_array "$suite_name" "sources" " ")
    local dependencies_str=$(get_config_array "$suite_name" "dependencies" "|||")
    local binaries_str=$(get_config_array "$suite_name" "binaries" " ")
    local special_flags=$(get_config "$suite_name" "special_flags")
    local uses_build_config=$(get_config "$suite_name" "uses_build_config")
    
    # Parse arrays using bash 3.2 compatible method
    IFS=' ' read -ra sources_array <<< "$sources_str"
    IFS=' ' read -ra binaries_array <<< "$binaries_str"
    
    # Parse dependencies using custom function
    local dependencies_array=()
    while IFS= read -r dep; do
        dependencies_array+=("$dep")
    done < <(parse_array_string "$dependencies_str" "|||")
    
    local total_failed=0
    local total_tests=0
    local total_passed=0
    
    # Arrays to store results for this suite
    local test_results=()
    local test_names=()
    local test_totals=()
    local test_passed=()
    local test_failed=()
    
    # Arrays for parallel execution
    local job_pids=()
    local result_files=()
    
    # Create temporary directory
    local temp_dir=$(mktemp -d)
    
    # Start each test in parallel
    for i in "${!sources_array[@]}"; do
        local test_source="${sources_array[$i]}"
        local test_binary="${binaries_array[$i]}"
        local test_deps="${dependencies_array[$i]}"
        local test_name="${sources_array[$i]%%.c}"
        local result_file="$temp_dir/result_$i.txt"
        
        # Add test/ prefix if not already present
        if [[ "$test_source" != test/* ]]; then
            test_source="test/$test_source"
        fi
        if [[ "$test_binary" != test/* ]]; then
            test_binary="test/$test_binary"
        fi
        
        print_status "Compiling $test_source..."
        
        local compile_cmd=""
        
        # Handle different compilation methods based on configuration
        if [ "$uses_build_config" = "true" ]; then
            # Build config-based compilation (for MIR and Lambda)
            local config_file=$(get_build_config_file "$suite_name")
            if [ ! -f "$config_file" ]; then
                print_error "Configuration file $config_file not found!"
                total_failed=$((total_failed + 1))
                continue
            fi
            
            # Extract and build object files list
            local object_files=()
            while IFS= read -r source_file; do
                local base_name
                if [[ "$source_file" == *.cpp ]]; then
                    base_name=$(basename "$source_file" .cpp)
                else
                    base_name=$(basename "$source_file" .c)
                fi
                local obj_file="build/${base_name}.o"
                if [ -f "$obj_file" ]; then
                    object_files+=("$PWD/$obj_file")
                fi
            done < <(jq -r '.source_files[]' "$config_file" | grep -E '\.(c|cpp)$')
            
            # Process source_dirs if any
            local source_dirs
            source_dirs=$(jq -r '.source_dirs[]?' "$config_file" 2>/dev/null)
            if [ -n "$source_dirs" ]; then
                while IFS= read -r source_dir; do
                    if [ -d "$source_dir" ]; then
                        while IFS= read -r source_file; do
                            local rel_path="${source_file#$PWD/}"
                            local base_name
                            if [[ "$rel_path" == *.cpp ]]; then
                                base_name=$(basename "$rel_path" .cpp)
                            else
                                base_name=$(basename "$rel_path" .c)
                            fi
                            local obj_file="build/${base_name}.o"
                            if [ -f "$obj_file" ]; then
                                object_files+=("$PWD/$obj_file")
                            fi
                        done < <(find "$source_dir" -name "*.c" -o -name "*.cpp" -type f)
                    fi
                done <<< "$source_dirs"
            fi
            
            # Exclude main.o since Criterion provides its own main function
            local filtered_object_files=()
            for obj_file in "${object_files[@]}"; do
                if [[ "$obj_file" != *"/main.o" ]]; then
                    filtered_object_files+=("$obj_file")
                fi
            done
            object_files=("${filtered_object_files[@]}")
            
            # Build library flags
            local include_flags=""
            local static_libs=""
            local dynamic_libs=""
            local libraries
            libraries=$(jq -r '.libraries // []' "$config_file")
            
            if [ "$libraries" != "null" ] && [ "$libraries" != "[]" ]; then
                while IFS= read -r lib_info; do
                    local name include lib link
                    name=$(echo "$lib_info" | jq -r '.name')
                    include=$(echo "$lib_info" | jq -r '.include // empty')
                    lib=$(echo "$lib_info" | jq -r '.lib // empty')
                    link=$(echo "$lib_info" | jq -r '.link // "static"')
                    
                    if [ -n "$include" ] && [ "$include" != "null" ]; then
                        include_flags="$include_flags -I$include"
                    fi
                    
                    if [ "$link" = "static" ]; then
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            static_libs="$static_libs $lib"
                        fi
                    else
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            dynamic_libs="$dynamic_libs -L$lib -l$name"
                        fi
                    fi
                done < <(echo "$libraries" | jq -c '.[]')
            fi
            
            # Special handling for lambda tests
            if [ "$suite_name" = "lambda" ]; then
                include_flags="$include_flags -I./lib/mem-pool/include -I./lambda -I./lib"
                include_flags="$include_flags -I/opt/homebrew/include -I/opt/homebrew/Cellar/criterion/2.4.2_2/include"
                dynamic_libs="$dynamic_libs -L/opt/homebrew/lib -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
                compile_cmd="clang $special_flags $include_flags $CRITERION_FLAGS -o $test_binary $test_source ${object_files[*]} $static_libs $dynamic_libs"
            else
                compile_cmd="gcc $special_flags $include_flags $CRITERION_FLAGS -o $test_binary $test_source ${object_files[*]} $static_libs $dynamic_libs"
            fi
        else
            # Standard compilation (for library and input tests)
            compile_cmd="clang -o $test_binary $test_source $test_deps $CRITERION_FLAGS $special_flags"
        fi
        
        if $compile_cmd 2>/dev/null; then
            print_success "Compiled $test_source successfully"
            
            # Start test in background
            print_status "Starting $test_binary in parallel..."
            (
                set +e
                local output=$(./"$test_binary" --verbose --tap 2>&1)
                local exit_code=$?
                set -e
                
                # Parse results - handle both TAP and Criterion output formats
                local test_total=0
                local test_failed=0
                
                if echo "$output" | grep -q "^ok "; then
                    # TAP format
                    test_total=$(echo "$output" | grep -c "^ok " 2>/dev/null || echo "0")
                    test_failed=$(echo "$output" | grep -c "^not ok " 2>/dev/null || echo "0")
                elif echo "$output" | grep -q "Synthesis:"; then
                    # Criterion synthesis format
                    local synthesis_line=$(echo "$output" | grep "Synthesis:" | tail -1)
                    test_total=$(echo "$synthesis_line" | grep -o "Tested: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$(echo "$synthesis_line" | grep -o "Failing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    local crashing_tests=$(echo "$synthesis_line" | grep -o "Crashing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$((test_failed + crashing_tests))
                else
                    # Fallback based on exit code
                    if [ $exit_code -eq 0 ]; then
                        test_total=1
                        test_failed=0
                    else
                        test_total=1
                        test_failed=1
                    fi
                fi
                
                test_total=$(echo "$test_total" | tr -cd '0-9')
                test_failed=$(echo "$test_failed" | tr -cd '0-9')
                test_total=${test_total:-0}
                test_failed=${test_failed:-0}
                local test_passed=$((test_total - test_failed))
                
                # Write results
                echo "TEST_NAME:$test_name" > "$result_file"
                echo "TEST_TOTAL:$test_total" >> "$result_file"
                echo "TEST_PASSED:$test_passed" >> "$result_file"
                echo "TEST_FAILED:$test_failed" >> "$result_file"
                echo "TEST_OUTPUT_START" >> "$result_file"
                echo "$output" >> "$result_file"
                echo "TEST_OUTPUT_END" >> "$result_file"
                
                # Cleanup
                if [ -f "$test_binary" ]; then
                    rm "$test_binary"
                fi
                
                exit $test_failed
            ) &
            
            job_pids+=($!)
            result_files+=("$result_file")
        else
            print_error "Failed to compile $test_source"
            $compile_cmd
            total_failed=$((total_failed + 1))
            
            test_names+=("$test_name")
            test_totals+=(0)
            test_passed+=(0)
            test_failed+=(1)
        fi
    done
    
    # Wait for all jobs and collect results
    print_status "⏳ Waiting for parallel test execution to complete..."
    
    for i in "${!job_pids[@]}"; do
        local pid="${job_pids[$i]}"
        local result_file="${result_files[$i]}"
        
        wait $pid
        
        if [ -f "$result_file" ]; then
            local test_name=$(grep "^TEST_NAME:" "$result_file" | cut -d: -f2)
            local test_total=$(grep "^TEST_TOTAL:" "$result_file" | cut -d: -f2)
            local test_passed=$(grep "^TEST_PASSED:" "$result_file" | cut -d: -f2)
            local test_failed=$(grep "^TEST_FAILED:" "$result_file" | cut -d: -f2)
            
            local test_output=$(sed -n '/^TEST_OUTPUT_START$/,/^TEST_OUTPUT_END$/p' "$result_file" | sed '1d;$d')
            
            print_status "📋 Results for $test_name:"
            echo "$test_output"
            echo ""
            
            test_names+=("$test_name")
            test_totals+=($test_total)
            test_passed+=($test_passed)
            test_failed+=($test_failed)
            
            total_tests=$((total_tests + test_total))
            total_failed=$((total_failed + test_failed))
        else
            print_error "Failed to read results for job $i"
            total_failed=$((total_failed + 1))
        fi
    done
    
    # Cleanup
    rm -rf "$temp_dir"
    
    total_passed=$((total_tests - total_failed))
    
    # Store results using generic approach
    store_suite_results "$suite_name" "$total_tests" "$total_passed" "$total_failed" \
                       test_names test_totals test_passed test_failed
    
    echo ""
    local suite_display_name=$(get_config "$suite_name" "name")
    print_status "📊 $suite_display_name Results Summary:"
    echo "   Total Tests: $total_tests"
    echo "   Passed: $total_passed"
    echo "   Failed: $total_failed"
    
    # Demonstrate the generic results retrieval API
    display_suite_summary "$suite_name"
    
    return $total_failed
}

# Implementation for sequential test suites
run_sequential_suite_impl() {
    local suite_name="$1"
    # This can be implemented if needed for sequential execution
    # For now, just call the parallel implementation
    run_parallel_suite_impl "$suite_name"
    return $?
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
    run_common_test_suite "validator"
    return $?
}

# Function to compile and run library tests
run_library_tests() {
    run_common_test_suite "library" "true"
    return $?
}

# Function to compile and run input processing tests
run_input_tests() {
    run_common_test_suite "input" "true"
    return $?
}

# Function to compile and run MIR JIT tests
run_mir_tests() {
    run_common_test_suite "mir"
    return $?
}

# Function to compile and run lambda runtime tests
run_lambda_tests() {
    run_common_test_suite "lambda"
    return $?
}

# Common function to run individual tests for different suite types
run_individual_test() {
    local suite_type="$1"
    local test_name="$2"
    
    # Load configuration if not already loaded
    if [ ${#TEST_SUITE_NAMES[@]} -eq 0 ]; then
        load_test_config
    fi
    
    # Get suite configuration
    local sources_str=$(get_config_array "$suite_type" "sources" " ")
    local binaries_str=$(get_config_array "$suite_type" "binaries" " ")
    local special_flags=$(get_config "$suite_type" "special_flags")
    
    # Parse arrays using bash 3.2 compatible method
    IFS=' ' read -ra sources_array <<< "$sources_str"
    IFS=' ' read -ra binaries_array <<< "$binaries_str"
    
    local source=""
    local binary=""
    local deps=""
    
    # Handle different suite types differently
    if [ "$suite_type" = "validator" ]; then
        # Validator has only one test
        source="${sources_array[0]}"
        binary="${binaries_array[0]}"
        deps=""
    elif [ "$suite_type" = "mir" ]; then
        # MIR has only one test
        source="${sources_array[0]}"
        binary="${binaries_array[0]}"
        deps=""
    elif [ "$suite_type" = "lambda" ]; then
        # Lambda has only one test
        source="${sources_array[0]}"
        binary="${binaries_array[0]}"
        deps=""
    else
        # Library and input tests: find specific test by name
        local dependencies_str=$(get_config_array "$suite_type" "dependencies" "|||")
        
        # Parse dependencies using custom function
        local dependencies_array=()
        while IFS= read -r dep; do
            dependencies_array+=("$dep")
        done < <(parse_array_string "$dependencies_str" "|||")
        
        local test_index=-1
        for i in "${!sources_array[@]}"; do
            if [[ "${sources_array[$i]}" == "test_${test_name}.c" ]]; then
                test_index=$i
                break
            fi
        done
        
        if [ $test_index -eq -1 ]; then
            print_error "Test '$test_name' not found in $suite_type tests"
            print_status "Available $suite_type tests:"
            for source_file in "${sources_array[@]}"; do
                test_basename=$(basename "$source_file" .c)
                echo "  - ${test_basename#test_}"
            done
            return 1
        fi
        
        source="${sources_array[$test_index]}"
        binary="${binaries_array[$test_index]}"
        deps="${dependencies_array[$test_index]}"
    fi
    
    # Handle raw mode
    if [ "$RAW_OUTPUT" = true ]; then
        local raw_compile_cmd=""
        local raw_binary_path=""
        
        if [ "$suite_type" = "validator" ]; then
            raw_compile_cmd="gcc -std=c99 -Wall -Wextra -g $special_flags $CRITERION_FLAGS -o $binary $source"
            raw_binary_path="$binary"
        elif [ "$suite_type" = "mir" ]; then
            # MIR tests need special compilation with object files from build config
            # Use the same compilation logic as run_mir_suite_impl
            local config_file="build_lambda_config.json"
            if [ ! -f "$config_file" ]; then
                print_error "Configuration file $config_file not found!"
                return 1
            fi
            
            # Extract and build object files list (same as run_mir_suite_impl)
            local object_files=()
            while IFS= read -r source_file; do
                local base_name
                if [[ "$source_file" == *.cpp ]]; then
                    base_name=$(basename "$source_file" .cpp)
                else
                    base_name=$(basename "$source_file" .c)
                fi
                local obj_file="build/${base_name}.o"
                if [ -f "$obj_file" ]; then
                    object_files+=("$PWD/$obj_file")
                fi
            done < <(jq -r '.source_files[]' "$config_file" | grep -E '\.(c|cpp)$')
            
            # Process source_dirs if any
            local source_dirs
            source_dirs=$(jq -r '.source_dirs[]?' "$config_file" 2>/dev/null)
            if [ -n "$source_dirs" ]; then
                while IFS= read -r source_dir; do
                    if [ -d "$source_dir" ]; then
                        while IFS= read -r source_file; do
                            local rel_path="${source_file#$PWD/}"
                            local base_name
                            if [[ "$rel_path" == *.cpp ]]; then
                                base_name=$(basename "$rel_path" .cpp)
                            else
                                base_name=$(basename "$rel_path" .c)
                            fi
                            local obj_file="build/${base_name}.o"
                            if [ -f "$obj_file" ]; then
                                object_files+=("$PWD/$obj_file")
                            fi
                        done < <(find "$source_dir" -name "*.c" -o -name "*.cpp" -type f)
                    fi
                done <<< "$source_dirs"
            fi
            
            # For MIR tests, exclude main.o since Criterion provides its own main function
            local filtered_object_files=()
            for obj_file in "${object_files[@]}"; do
                if [[ "$obj_file" != *"/main.o" ]]; then
                    filtered_object_files+=("$obj_file")
                fi
            done
            object_files=("${filtered_object_files[@]}")
            
            # Build library flags
            local include_flags=""
            local static_libs=""
            local dynamic_libs=""
            local libraries
            libraries=$(jq -r '.libraries // []' "$config_file")
            
            if [ "$libraries" != "null" ] && [ "$libraries" != "[]" ]; then
                while IFS= read -r lib_info; do
                    local name include lib link
                    name=$(echo "$lib_info" | jq -r '.name')
                    include=$(echo "$lib_info" | jq -r '.include // empty')
                    lib=$(echo "$lib_info" | jq -r '.lib // empty')
                    link=$(echo "$lib_info" | jq -r '.link // "static"')
                    
                    if [ -n "$include" ] && [ "$include" != "null" ]; then
                        include_flags="$include_flags -I$include"
                    fi
                    
                    if [ "$link" = "static" ]; then
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            static_libs="$static_libs $lib"
                        fi
                    else
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            dynamic_libs="$dynamic_libs -L$lib -l$name"
                        fi
                    fi
                done < <(echo "$libraries" | jq -c '.[]')
            fi
            
            raw_compile_cmd="gcc $special_flags $include_flags $CRITERION_FLAGS -o $binary $source ${object_files[*]} $static_libs $dynamic_libs"
            raw_binary_path="$binary"
        elif [ "$suite_type" = "lambda" ]; then
            # Lambda tests need special compilation with object files from build config
            # Use the same compilation logic as MIR tests
            local config_file="build_lambda_config.json"
            if [ ! -f "$config_file" ]; then
                print_error "Configuration file $config_file not found!"
                return 1
            fi
            
            # Extract and build object files list (same as run_mir_suite_impl)
            local object_files=()
            while IFS= read -r source_file; do
                local base_name
                if [[ "$source_file" == *.cpp ]]; then
                    base_name=$(basename "$source_file" .cpp)
                else
                    base_name=$(basename "$source_file" .c)
                fi
                local obj_file="build/${base_name}.o"
                if [ -f "$obj_file" ]; then
                    object_files+=("$PWD/$obj_file")
                fi
            done < <(jq -r '.source_files[]' "$config_file" | grep -E '\.(c|cpp)$')
            
            # Process source_dirs if any
            local source_dirs
            source_dirs=$(jq -r '.source_dirs[]?' "$config_file" 2>/dev/null)
            if [ -n "$source_dirs" ]; then
                while IFS= read -r source_dir; do
                    if [ -d "$source_dir" ]; then
                        while IFS= read -r source_file; do
                            local rel_path="${source_file#$PWD/}"
                            local base_name
                            if [[ "$rel_path" == *.cpp ]]; then
                                base_name=$(basename "$rel_path" .cpp)
                            else
                                base_name=$(basename "$rel_path" .c)
                            fi
                            local obj_file="build/${base_name}.o"
                            if [ -f "$obj_file" ]; then
                                object_files+=("$PWD/$obj_file")
                            fi
                        done < <(find "$source_dir" -name "*.c" -o -name "*.cpp" -type f)
                    fi
                done <<< "$source_dirs"
            fi
            
            # For Lambda tests, exclude main.o since Criterion provides its own main function
            local filtered_object_files=()
            for obj_file in "${object_files[@]}"; do
                if [[ "$obj_file" != *"/main.o" ]]; then
                    filtered_object_files+=("$obj_file")
                fi
            done
            object_files=("${filtered_object_files[@]}")
            
            # Build library flags
            local include_flags=""
            local static_libs=""
            local dynamic_libs=""
            local libraries
            libraries=$(jq -r '.libraries // []' "$config_file")
            
            if [ "$libraries" != "null" ] && [ "$libraries" != "[]" ]; then
                while IFS= read -r lib_info; do
                    local name include lib link
                    name=$(echo "$lib_info" | jq -r '.name')
                    include=$(echo "$lib_info" | jq -r '.include // empty')
                    lib=$(echo "$lib_info" | jq -r '.lib // empty')
                    link=$(echo "$lib_info" | jq -r '.link // "static"')
                    
                    if [ -n "$include" ] && [ "$include" != "null" ]; then
                        include_flags="$include_flags -I$include"
                    fi
                    
                    if [ "$link" = "static" ]; then
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            static_libs="$static_libs $lib"
                        fi
                    else
                        if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                            dynamic_libs="$dynamic_libs -L$lib -l$name"
                        fi
                    fi
                done < <(echo "$libraries" | jq -c '.[]')
            fi
            
            # Add additional includes and libraries for lambda tests
            include_flags="$include_flags -I./lib/mem-pool/include -I./lambda -I./lib"
            include_flags="$include_flags -I/opt/homebrew/include -I/opt/homebrew/Cellar/criterion/2.4.2_2/include"
            dynamic_libs="$dynamic_libs -L/opt/homebrew/lib -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
            
            raw_compile_cmd="clang $special_flags $include_flags $CRITERION_FLAGS -o $binary $source ${object_files[*]} $static_libs $dynamic_libs"
            raw_binary_path="$binary"
        else
            raw_compile_cmd="gcc -std=c99 -Wall -Wextra -g -O0 -I. -Ilambda -Ilib $CRITERION_FLAGS -o test/$binary test/$source $deps $special_flags"
            raw_binary_path="test/$binary"
        fi
        
        if $raw_compile_cmd 2>/dev/null; then
            run_raw_test "$raw_binary_path" "$test_name"
            return $?
        else
            echo "❌ Failed to compile $source"
            $raw_compile_cmd
            return 1
        fi
    fi
    
    # Check prerequisites for specific suite types
    if [ "$suite_type" = "validator" ]; then
        if [ ! -f "./lambda.exe" ]; then
            print_error "Lambda executable not found. Run 'make' first."
            return 1
        fi
        
        # Set environment variables from config for validator
        local env_vars=$(jq -r ".tests[] | select(.suite == \"$suite_type\") | .environment // {} | to_entries | map(\"\(.key)=\(.value)\") | join(\"|||\")" "$TEST_CONFIG_FILE" 2>/dev/null || echo "")
        if [ -n "$env_vars" ] && [ "$env_vars" != "null" ]; then
            IFS='|||' read -ra ENV_ARRAY <<< "$env_vars"
            for env_var in "${ENV_ARRAY[@]}"; do
                if [ -n "$env_var" ]; then
                    # Expand variables in the environment value
                    expanded_env_var=$(eval echo "$env_var")
                    export "$expanded_env_var"
                    print_status "Set environment: $expanded_env_var"
                fi
            done
        fi
    fi
    
    # Print status and compile
    if [ "$suite_type" = "validator" ]; then
        print_status "🧪 Running individual validator test: $test_name"
        print_status "Compiling validator test..."
        local compile_cmd="gcc -std=c99 -Wall -Wextra -g $special_flags $CRITERION_FLAGS -o $binary $source"
        local binary_path="./$binary"
    elif [ "$suite_type" = "mir" ]; then
        print_status "🧪 Running individual MIR test: $test_name"
        print_status "Compiling MIR test..."
        
        # Use the same compilation logic as the raw mode for MIR
        local config_file="build_lambda_config.json"
        if [ ! -f "$config_file" ]; then
            print_error "Configuration file $config_file not found!"
            return 1
        fi
        
        # Extract and build object files list (same as run_mir_suite_impl)
        local object_files=()
        while IFS= read -r source_file; do
            local base_name
            if [[ "$source_file" == *.cpp ]]; then
                base_name=$(basename "$source_file" .cpp)
            else
                base_name=$(basename "$source_file" .c)
            fi
            local obj_file="build/${base_name}.o"
            if [ -f "$obj_file" ]; then
                object_files+=("$PWD/$obj_file")
            fi
        done < <(jq -r '.source_files[]' "$config_file" | grep -E '\.(c|cpp)$')
        
        # Process source_dirs if any
        local source_dirs
        source_dirs=$(jq -r '.source_dirs[]?' "$config_file" 2>/dev/null)
        if [ -n "$source_dirs" ]; then
            while IFS= read -r source_dir; do
                if [ -d "$source_dir" ]; then
                    while IFS= read -r source_file; do
                        local rel_path="${source_file#$PWD/}"
                        local base_name
                        if [[ "$rel_path" == *.cpp ]]; then
                            base_name=$(basename "$rel_path" .cpp)
                        else
                            base_name=$(basename "$rel_path" .c)
                        fi
                        local obj_file="build/${base_name}.o"
                        if [ -f "$obj_file" ]; then
                            object_files+=("$PWD/$obj_file")
                        fi
                    done < <(find "$source_dir" -name "*.c" -o -name "*.cpp" -type f)
                fi
            done <<< "$source_dirs"
        fi
        
        # For MIR tests, exclude main.o since Criterion provides its own main function
        local filtered_object_files=()
        for obj_file in "${object_files[@]}"; do
            if [[ "$obj_file" != *"/main.o" ]]; then
                filtered_object_files+=("$obj_file")
            fi
        done
        object_files=("${filtered_object_files[@]}")
        
        # Build library flags
        local include_flags=""
        local static_libs=""
        local dynamic_libs=""
        local libraries
        libraries=$(jq -r '.libraries // []' "$config_file")
        
        if [ "$libraries" != "null" ] && [ "$libraries" != "[]" ]; then
            while IFS= read -r lib_info; do
                local name include lib link
                name=$(echo "$lib_info" | jq -r '.name')
                include=$(echo "$lib_info" | jq -r '.include // empty')
                lib=$(echo "$lib_info" | jq -r '.lib // empty')
                link=$(echo "$lib_info" | jq -r '.link // "static"')
                
                if [ -n "$include" ] && [ "$include" != "null" ]; then
                    include_flags="$include_flags -I$include"
                fi
                
                if [ "$link" = "static" ]; then
                    if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                        static_libs="$static_libs $lib"
                    fi
                else
                    if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                        dynamic_libs="$dynamic_libs -L$lib -l$name"
                    fi
                fi
            done < <(echo "$libraries" | jq -c '.[]')
        fi
        
        local compile_cmd="gcc $special_flags $include_flags $CRITERION_FLAGS -o $binary $source ${object_files[*]} $static_libs $dynamic_libs"
        local binary_path="./$binary"
    elif [ "$suite_type" = "lambda" ]; then
        print_status "🧪 Running individual Lambda test: $test_name"
        print_status "Compiling Lambda test..."
        
        # Use the same compilation logic as MIR tests for Lambda
        local config_file="build_lambda_config.json"
        if [ ! -f "$config_file" ]; then
            print_error "Configuration file $config_file not found!"
            return 1
        fi
        
        # Extract and build object files list (same as run_mir_suite_impl)
        local object_files=()
        while IFS= read -r source_file; do
            local base_name
            if [[ "$source_file" == *.cpp ]]; then
                base_name=$(basename "$source_file" .cpp)
            else
                base_name=$(basename "$source_file" .c)
            fi
            local obj_file="build/${base_name}.o"
            if [ -f "$obj_file" ]; then
                object_files+=("$PWD/$obj_file")
            fi
        done < <(jq -r '.source_files[]' "$config_file" | grep -E '\.(c|cpp)$')
        
        # Process source_dirs if any
        local source_dirs
        source_dirs=$(jq -r '.source_dirs[]?' "$config_file" 2>/dev/null)
        if [ -n "$source_dirs" ]; then
            while IFS= read -r source_dir; do
                if [ -d "$source_dir" ]; then
                    while IFS= read -r source_file; do
                        local rel_path="${source_file#$PWD/}"
                        local base_name
                        if [[ "$rel_path" == *.cpp ]]; then
                            base_name=$(basename "$rel_path" .cpp)
                        else
                            base_name=$(basename "$rel_path" .c)
                        fi
                        local obj_file="build/${base_name}.o"
                        if [ -f "$obj_file" ]; then
                            object_files+=("$PWD/$obj_file")
                        fi
                    done < <(find "$source_dir" -name "*.c" -o -name "*.cpp" -type f)
                fi
            done <<< "$source_dirs"
        fi
        
        # For Lambda tests, exclude main.o since Criterion provides its own main function
        local filtered_object_files=()
        for obj_file in "${object_files[@]}"; do
            if [[ "$obj_file" != *"/main.o" ]]; then
                filtered_object_files+=("$obj_file")
            fi
        done
        object_files=("${filtered_object_files[@]}")
        
        # Build library flags
        local include_flags=""
        local static_libs=""
        local dynamic_libs=""
        local libraries
        libraries=$(jq -r '.libraries // []' "$config_file")
        
        if [ "$libraries" != "null" ] && [ "$libraries" != "[]" ]; then
            while IFS= read -r lib_info; do
                local name include lib link
                name=$(echo "$lib_info" | jq -r '.name')
                include=$(echo "$lib_info" | jq -r '.include // empty')
                lib=$(echo "$lib_info" | jq -r '.lib // empty')
                link=$(echo "$lib_info" | jq -r '.link // "static"')
                
                if [ -n "$include" ] && [ "$include" != "null" ]; then
                    include_flags="$include_flags -I$include"
                fi
                
                if [ "$link" = "static" ]; then
                    if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                        static_libs="$static_libs $lib"
                    fi
                else
                    if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                        dynamic_libs="$dynamic_libs -L$lib -l$name"
                    fi
                fi
            done < <(echo "$libraries" | jq -c '.[]')
        fi
        
        # Add additional includes and libraries for lambda tests
        include_flags="$include_flags -I./lib/mem-pool/include -I./lambda -I./lib"
        include_flags="$include_flags -I/opt/homebrew/include -I/opt/homebrew/Cellar/criterion/2.4.2_2/include"
        dynamic_libs="$dynamic_libs -L/opt/homebrew/lib -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
        
        local compile_cmd="clang $special_flags $include_flags $CRITERION_FLAGS -o $binary $source ${object_files[*]} $static_libs $dynamic_libs"
        local binary_path="./$binary"
    else
        print_status "🧪 Running individual test: $test_name"
        print_status "Compiling test/test_$test_name.c..."
        local compile_cmd="gcc -std=c99 -Wall -Wextra -g -O0 -I. -Ilambda -Ilib $CRITERION_FLAGS -o test/$binary test/$source $deps $special_flags"
        local binary_path="./test/$binary"
    fi
    
    if $compile_cmd 2>/dev/null; then
        print_success "Compiled $source successfully"
    else
        print_error "Failed to compile $source"
        print_error "Attempting compilation with detailed error output..."
        $compile_cmd
        return 1
    fi
    
    print_status "🧪 Running $test_name tests..."
    echo ""
    
    # Run the test with appropriate flags
    set +e
    local test_output
    local test_command=""
    
    if [ "$suite_type" = "validator" ]; then
        test_command="$binary_path --verbose --tap --jobs=$CPU_CORES"
    else
        test_command="$binary_path"
    fi
    
    if command -v timeout >/dev/null 2>&1; then
        test_output=$(timeout 30 $test_command 2>&1)
        local test_exit_code=$?
        if [ $test_exit_code -eq 124 ]; then
            print_error "Test $test_name timed out after 30 seconds"
            test_output="Test timed out"
            test_exit_code=1
        fi
    else
        test_output=$($test_command 2>&1)
        test_exit_code=$?
    fi
    set -e
    
    echo "$test_output"
    echo ""
    
    # Parse results - different parsing for different output formats
    local total_tests=0
    local failed_tests=0
    
    if [ "$suite_type" = "validator" ]; then
        # Parse TAP output for validator
        total_tests=$(echo "$test_output" | grep -c "^ok " 2>/dev/null || echo "0")
        failed_tests=$(echo "$test_output" | grep -c "^not ok " 2>/dev/null || echo "0")
    else
        # Parse Criterion output for library/input/mir tests
        if echo "$test_output" | grep -q "Synthesis:"; then
            local synthesis_line
            synthesis_line=$(echo "$test_output" | grep "Synthesis:" | tail -1)
            total_tests=$(echo "$synthesis_line" | grep -o "Tested: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
            failed_tests=$(echo "$synthesis_line" | grep -o "Failing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
            local crashing_tests
            crashing_tests=$(echo "$synthesis_line" | grep -o "Crashing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
            failed_tests=$((failed_tests + crashing_tests))
        else
            if [ $test_exit_code -eq 0 ]; then
                total_tests=1
                failed_tests=0
            else
                total_tests=1
                failed_tests=1
            fi
        fi
    fi
    
    # Clean up binary
    if [ -f "$binary_path" ]; then
        rm "$binary_path"
    fi
    
    # Clean numeric values
    total_tests=$(echo "$total_tests" | tr -cd '0-9')
    failed_tests=$(echo "$failed_tests" | tr -cd '0-9')
    total_tests=${total_tests:-0}
    failed_tests=${failed_tests:-0}
    
    local passed_tests=$((total_tests - failed_tests))
    echo "================================================"
    print_status "📊 $test_name Test Results:"
    print_status "   Total: $total_tests, Passed: $passed_tests, Failed: $failed_tests"
    
    if [ $failed_tests -eq 0 ] && [ $total_tests -gt 0 ]; then
        print_success "All $test_name tests passed! ✨"
    elif [ $total_tests -eq 0 ]; then
        print_warning "No tests were detected for $test_name"
    else
        print_error "$failed_tests $test_name test(s) failed"
    fi
    echo "================================================"
    
    return $failed_tests
}

# Function to run individual library test
run_individual_library_test() {
    run_individual_test "library" "$1"
    return $?
}

# Function to run individual validator test
run_individual_validator_test() {
    run_individual_test "validator" "$1"
    return $?
}

# Function to run individual input test
run_individual_input_test() {
    run_individual_test "input" "$1"
    return $?
}

# Function to run individual MIR test
run_individual_mir_test() {
    run_individual_test "mir" "$1"
    return $?
}

# Function to run individual Lambda test
run_individual_lambda_test() {
    run_individual_test "lambda" "$1"
    return $?
}

# Function to run target test suites
run_target_test() {
    local target="$1"
    
    case "$target" in
        "all")
            # Raw mode not supported for "all" target
            if [ "$RAW_OUTPUT" = true ]; then
                print_error "--raw option is not supported with --target=all"
                print_status "Use --raw with individual tests like: --target=math --raw"
                return 1
            fi
            # Run all tests (existing behavior)
            return 0
            ;;
        "library")
            if [ "$RAW_OUTPUT" = true ]; then
                print_error "--raw option is not supported with suite targets"
                print_status "Use --raw with individual tests like: --target=strbuf --raw"
                return 1
            fi
            print_status "🚀 Running Library Tests Suite"
            run_library_tests
            return $?
            ;;
        "input")
            if [ "$RAW_OUTPUT" = true ]; then
                print_error "--raw option is not supported with suite targets"
                print_status "Use --raw with individual tests like: --target=math --raw"
                return 1
            fi
            print_status "🚀 Running Input Processing Tests Suite"
            run_input_tests
            return $?
            ;;
        "validator")
            if [ "$RAW_OUTPUT" = true ]; then
                run_individual_validator_test "validator"
                return $?
            fi
            print_status "🚀 Running Validator Tests Suite"
            run_validator_tests
            return $?
            ;;
        "mir")
            if [ "$RAW_OUTPUT" = true ]; then
                run_individual_mir_test "mir"
                return $?
            fi
            print_status "🚀 Running MIR JIT Tests Suite"
            run_mir_tests
            return $?
            ;;
        "lambda")
            if [ "$RAW_OUTPUT" = true ]; then
                run_individual_lambda_test "lambda"
                return $?
            fi
            print_status "🚀 Running Lambda Runtime Tests Suite"
            run_lambda_tests
            return $?
            ;;
        "strbuf"|"strview"|"variable_pool"|"num_stack")
            run_individual_library_test "$target"
            return $?
            ;;
        "mime_detect"|"math")
            run_individual_input_test "$target"
            return $?
            ;;
        *)
            print_error "Unknown target: $target"
            print_status "Available targets: all, library, input, validator, mir, lambda, strbuf, strview, variable_pool, num_stack, mime_detect, math"
            return 1
            ;;
    esac
}

# Main execution
echo ""
print_status "🚀 Starting comprehensive test suite..."

# Load test configuration
load_test_config

# Detect CPU cores for parallel execution
detect_cpu_cores

# Find Criterion installation
find_criterion

# Check if we should run individual test or all tests
if [ "$TARGET_TEST" != "all" ]; then
    print_status "🎯 Running target: $TARGET_TEST"
    echo ""
    
    # Run the specific target test
    if run_target_test "$TARGET_TEST"; then
        print_success "Target test '$TARGET_TEST' completed successfully! ✨"
        exit 0
    else
        print_error "Target test '$TARGET_TEST' failed!"
        exit 1
    fi
fi

# Continue with original parallel execution for "all" target
print_status "🚀 Running all test suites..."

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

# Start Input Processing tests in background
print_status "================================================"
print_status "      STARTING INPUT PROCESSING TESTS (PARALLEL)"
print_status "================================================"
INPUT_RESULT_FILE="$SUITE_TEMP_DIR/input_results.txt"
(
    # Run input tests and capture results
    print_status "📄 Input Processing Tests - Starting..."
    if run_input_tests; then
        input_failed=0
    else
        input_failed=$?
    fi
    
    # Write results to file (variables are available in this subshell)
    echo "SUITE_TYPE:INPUT" > "$INPUT_RESULT_FILE"
    echo "SUITE_NAME:📄 Input Processing Tests" >> "$INPUT_RESULT_FILE"
    echo "SUITE_TOTAL:$INPUT_TOTAL_TESTS" >> "$INPUT_RESULT_FILE"
    echo "SUITE_PASSED:$INPUT_PASSED_TESTS" >> "$INPUT_RESULT_FILE"
    echo "SUITE_FAILED:$INPUT_FAILED_TESTS" >> "$INPUT_RESULT_FILE"
    if [ $input_failed -eq 0 ]; then
        echo "SUITE_STATUS:PASSED" >> "$INPUT_RESULT_FILE"
    else
        echo "SUITE_STATUS:FAILED" >> "$INPUT_RESULT_FILE"
    fi
    
    # Output with prefix for identification
    echo "[INPUT] Input processing tests completed with exit code: $input_failed"
    exit $input_failed
) &
SUITE_JOB_PIDS+=($!)
SUITE_RESULT_FILES+=("$INPUT_RESULT_FILE")
SUITE_TYPES+=("INPUT")

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

# Start Lambda Runtime tests in background
print_status "================================================"
print_status "      STARTING LAMBDA RUNTIME TESTS (PARALLEL) "
print_status "================================================"
LAMBDA_RESULT_FILE="$SUITE_TEMP_DIR/lambda_results.txt"
(
    # Run Lambda tests and capture results
    print_status "🐑 Lambda Runtime Tests - Starting..."
    if run_lambda_tests; then
        lambda_failed=0
    else
        lambda_failed=$?
    fi
    
    # Write results to file (variables are available in this subshell)
    echo "SUITE_TYPE:LAMBDA" > "$LAMBDA_RESULT_FILE"
    echo "SUITE_NAME:🐑 Lambda Runtime Tests" >> "$LAMBDA_RESULT_FILE"
    echo "SUITE_TOTAL:$LAMBDA_TOTAL_TESTS" >> "$LAMBDA_RESULT_FILE"
    echo "SUITE_PASSED:$LAMBDA_PASSED_TESTS" >> "$LAMBDA_RESULT_FILE"
    echo "SUITE_FAILED:$LAMBDA_FAILED_TESTS" >> "$LAMBDA_RESULT_FILE"
    if [ $lambda_failed -eq 0 ]; then
        echo "SUITE_STATUS:PASSED" >> "$LAMBDA_RESULT_FILE"
    else
        echo "SUITE_STATUS:FAILED" >> "$LAMBDA_RESULT_FILE"
    fi
    
    # Output with prefix for identification
    echo "[LAMBDA] Lambda Runtime tests completed with exit code: $lambda_failed"
    exit $lambda_failed
) &
SUITE_JOB_PIDS+=($!)
SUITE_RESULT_FILES+=("$LAMBDA_RESULT_FILE")
SUITE_TYPES+=("LAMBDA")

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
for i in 0 1 2 3 4; do
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
        elif [ "$suite_type" = "INPUT" ]; then
            for j in "${!INPUT_TEST_NAMES[@]}"; do
                test_name="${INPUT_TEST_NAMES[$j]}"
                test_total="${INPUT_TEST_TOTALS[$j]}"
                test_passed="${INPUT_TEST_PASSED[$j]}"
                
                echo "   ├─ $test_name: $test_total tests (✅ $test_passed passed)"
            done
        elif [ "$suite_type" = "MIR" ]; then
            echo "   ├─ MIR JIT Tests: $suite_total tests (✅ $suite_passed passed)"
        elif [ "$suite_type" = "LAMBDA" ]; then
            echo "   ├─ Lambda Runtime Tests: $suite_total tests (✅ $suite_passed passed)"
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
        elif [ "$suite_type" = "INPUT" ]; then
            for j in "${!INPUT_TEST_NAMES[@]}"; do
                test_name="${INPUT_TEST_NAMES[$j]}"
                test_total="${INPUT_TEST_TOTALS[$j]}"
                test_passed="${INPUT_TEST_PASSED[$j]}"
                test_failed="${INPUT_TEST_FAILED[$j]}"
                
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
        elif [ "$suite_type" = "LAMBDA" ]; then
            if [ "$suite_failed" -eq 0 ]; then
                echo "   ├─ Lambda Runtime Tests: $suite_total tests (✅ $suite_passed passed) ✅"
            else
                echo "   ├─ Lambda Runtime Tests: $suite_total tests (✅ $suite_passed passed, ❌ $suite_failed failed) ❌"
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
        elif [ "$suite_type" = "INPUT" ]; then
            echo "   Input processing test failures: $suite_failed"
        elif [ "$suite_type" = "VALIDATOR" ]; then
            echo "   Validator test failures: $suite_failed"
        elif [ "$suite_type" = "MIR" ]; then
            echo "   MIR JIT test failures: $suite_failed"
        elif [ "$suite_type" = "LAMBDA" ]; then
            echo "   Lambda Runtime test failures: $suite_failed"
        fi
    done
    
    echo ""
    print_warning "⚠️  Review failed tests above for details"
    print_status "📋 See lambda/validator/validator.md for comprehensive test coverage information" 
    echo ""
    exit 1
fi
