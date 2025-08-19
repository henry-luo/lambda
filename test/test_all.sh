#!/usr/bin/env bash

# Lambda Comprehensive Test Suite Runner
# Compiles and runs all tests including validator and library tests as proper Criterion unit tests

set -e  # Exit on any error

# Source shared build utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -f "$SCRIPT_DIR/lib/build_utils.sh" ]; then
    source "$SCRIPT_DIR/lib/build_utils.sh"
    echo "Using enhanced build utilities (Phase 1)"
else
    echo "Warning: build_utils.sh not found, using legacy functions"
fi

# Handle SIGPIPE gracefully when output is piped to commands like grep
trap 'exit 0' SIGPIPE

# Detect if we're being piped and set a flag
if [ ! -t 1 ]; then
    PIPED_OUTPUT=true
    echo "Detected piped output - running tests in sequential mode to avoid hangs"
else
    PIPED_OUTPUT=false
fi

# Safe echo function that exits gracefully on broken pipe
safe_echo() {
    if [ "$PIPED_OUTPUT" = true ]; then
        echo "$@" 2>/dev/null || exit 0
    else
        echo "$@"
    fi
}

# Parse command line arguments
TARGET_TEST=""
SHOW_HELP=false
RAW_OUTPUT=false
KEEP_EXE=false

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
    echo "  markup_roundtrip  Run only markup roundtrip tests"
    echo ""
    echo "Configuration:"
    echo "  Test configurations are loaded from build_lambda_config.json (test section)"
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

safe_echo "================================================"
safe_echo "     Lambda Comprehensive Test Suite Runner    "
safe_echo "================================================"

# Display target information
if [ -n "$TARGET_TEST" ]; then
    safe_echo "🎯 Target: Running '$TARGET_TEST' tests only"
else
    safe_echo "🎯 Target: Running all test suites"
    TARGET_TEST="all"
fi
safe_echo ""

# Configuration file path
BUILD_CONFIG_FILE="build_lambda_config.json"

# Global variables for loaded test configuration
declare -a TEST_SUITE_NAMES
declare -a SUITE_CONFIGS

# Helper function to get configuration value
get_config() {
    local suite="$1"
    local key="$2"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ]; then
        echo ""
        return 1
    fi
    
    jq -r ".test.test_suites[] | select(.suite == \"$suite\") | .$key // \"\"" "$BUILD_CONFIG_FILE" 2>/dev/null || echo ""
}

# Helper function to get configuration array as string
get_config_array() {
    local suite="$1"
    local key="$2"
    local separator="${3:-|||}"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ]; then
        echo ""
        return 1
    fi
    
    jq -r ".test.test_suites[] | select(.suite == \"$suite\") | .$key | join(\"$separator\")" "$BUILD_CONFIG_FILE" 2>/dev/null || echo ""
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

# Function to get library dependencies for a specific test
get_library_dependencies_for_test() {
    local suite_type="$1"
    local test_index="${2:-0}"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        return 1
    fi
    
    # Get test suite definition from config
    local test_suite=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_type\")" "$BUILD_CONFIG_FILE" 2>/dev/null)
    if [ -z "$test_suite" ] || [ "$test_suite" = "null" ]; then
        return 1
    fi
    
    # Get library dependencies for specific test index
    local library_deps=$(echo "$test_suite" | jq -r ".library_dependencies[$test_index][]? // empty" | tr '\n' ' ')
    
    # Output space-separated library names
    echo "$library_deps"
}

# Helper function to build compilation command using library dependency resolution (Phase 5)
build_library_based_compile_cmd() {
    local suite_type="$1"
    local source="$2" 
    local binary="$3"
    local test_index="$4"
    local special_flags="$5"
    
    # Phase 6: Check build prerequisites before proceeding
    if ! check_build_prerequisites; then
        echo "Build prerequisites check failed. Please run 'make build' first."
        return 1
    fi
    
    # Try to get library dependencies first (Phase 5 approach)
    local lib_deps_str=$(get_library_dependencies_for_test "$suite_type" "$test_index")
    
    if [ -n "$lib_deps_str" ]; then
        # Phase 6: Enhanced library name resolution with object file optimization
        local lib_deps_array=($lib_deps_str)
        
        # Handle both prefixed and unprefixed paths
        local final_source="$source"
        local final_binary="$binary"
        
        # Add test/ prefix if not already present
        if [[ "$final_source" != test/* ]]; then
            final_source="test/$final_source"
        fi
        if [[ "$final_binary" != test/* ]]; then
            final_binary="test/$final_binary"
        fi
        
        # Get compiler from config
        local compiler=$(get_config "$suite_type" "compiler")
        if [ -z "$compiler" ] || [ "$compiler" = "null" ]; then
            compiler=$(get_global_compiler)
        fi
        
        # Get C++ flags if specified in config
        local cpp_flags=$(get_config "$suite_type" "cpp_flags")
        local final_flags="$special_flags"
        
        # For C++ files, use C++ variant of compiler and add C++ flags
        if [[ "$final_source" == *.cpp ]]; then
            if [ "$compiler" = "clang" ]; then
                compiler="clang++"
            elif [ "$compiler" = "gcc" ]; then
                compiler="g++"
            fi
            
            # Add C++ specific flags if defined
            if [ -n "$cpp_flags" ] && [ "$cpp_flags" != "null" ]; then
                final_flags="$special_flags $cpp_flags"
            fi
        fi
        
        # Phase 6: Use optimized object file approach when possible
        local use_objects=false
        local object_files=""
        local criterion_flags=""
        local static_libs=""
        
        # Check if we can use object files (non-criterion dependencies exist)
        local has_non_criterion=false
        for lib_name in "${lib_deps_array[@]}"; do
            if [ "$lib_name" != "criterion" ]; then
                has_non_criterion=true
                break
            fi
        done
        
        if [ "$has_non_criterion" = "true" ]; then
            # Try to get minimal object set from build directory
            local minimal_objects=$(get_minimal_object_set "$final_source" "${lib_deps_array[@]}")
            
            if [ -n "$minimal_objects" ]; then
                use_objects=true
                object_files="$minimal_objects"
                
                # Get include flags and static libraries from library resolution
                local resolved_flags=$(resolve_library_dependencies "${lib_deps_array[@]}")
                
                # Extract criterion flags separately
                for lib_name in "${lib_deps_array[@]}"; do
                    if [ "$lib_name" = "criterion" ]; then
                        if pkg-config --exists criterion 2>/dev/null; then
                            criterion_flags=$(pkg-config --cflags --libs criterion)
                        else
                            # Fallback criterion flags
                            criterion_flags="-I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
                        fi
                        break
                    fi
                done
                
                # Extract static libraries (but exclude object files that we're providing separately)
                static_libs=$(echo "$resolved_flags" | grep -o '\S*\.a\s*' | tr '\n' ' ')
                local include_flags=$(echo "$resolved_flags" | grep -o '\-I[^[:space:]]*' | tr '\n' ' ')
                local link_flags=$(echo "$resolved_flags" | grep -o '\-L[^[:space:]]*\s*\-l[^[:space:]]*' | tr '\n' ' ')
                
                # Add ICU libraries if needed for unicode support
                local icu_libs=""
                local icu_includes=""
                for lib_name in "${lib_deps_array[@]}"; do
                    if [[ "$lib_name" == *"input"* ]] || [[ "$lib_name" == *"lambda-core"* ]] || [[ "$lib_name" == *"lambda-runtime-full"* ]] || [[ "$lib_name" == *"lambda-input-core"* ]]; then
                        if [ -n "$ICU_LIBS" ]; then
                            icu_libs="$ICU_LIBS"
                        else
                            # Use hardcoded ICU libs if environment variable is not set
                            icu_libs="-L/Users/henryluo/Projects/Jubily/icu-compact/lib -licui18n -licuuc -licudata"
                        fi
                        if [ -n "$ICU_CFLAGS" ]; then
                            icu_includes="$ICU_CFLAGS"
                        else
                            # Use hardcoded ICU includes if environment variable is not set
                            icu_includes="-I/Users/henryluo/Projects/Jubily/icu-compact/include"
                        fi
                        break
                    fi
                done
                
                echo "$compiler $final_flags $icu_includes $include_flags -o $final_binary $final_source $object_files $static_libs $link_flags $icu_libs $criterion_flags"
                return 0
            fi
        fi
        
        # Phase 6: Fallback to legacy library resolution if object approach fails
        local resolved_flags=$(resolve_library_dependencies "${lib_deps_array[@]}")
        
        echo "$compiler $final_flags -o $final_binary $final_source $resolved_flags"
        return 0
    else
        # Fallback to legacy dependency strings if library_dependencies not available
        return 1
    fi
}

# Helper function to extract object files from build config (for validator/mir/lambda tests)
extract_build_object_files() {
    local config_file="$1"
    
    if [ ! -f "$config_file" ] || ! has_jq_support; then
        return 1
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
    
    # Return space-separated list
    printf "%s " "${filtered_object_files[@]}"
}

# Helper function to build generic compilation command from config
# Helper function to build dependency-based compilation command  
build_dependency_based_compile_cmd() {
    local suite_type="$1"
    local source="$2"
    local binary="$3"
    local deps="$4"
    local special_flags="$5"
    
    # Get C++ flags if specified in config
    local cpp_flags=$(get_config "$suite_type" "cpp_flags")
    local final_flags="$special_flags"
    
    # Handle both prefixed and unprefixed paths
    local final_source="$source"
    local final_binary="$binary"
    
    # Add test/ prefix if not already present
    if [[ "$final_source" != test/* ]]; then
        final_source="test/$final_source"
    fi
    if [[ "$final_binary" != test/* ]]; then
        final_binary="test/$final_binary"
    fi
    
    if [ "$suite_type" = "validator" ]; then
        # Get compiler from config for validator tests
        local config_compiler=$(get_config "validator" "compiler")
        local compiler="gcc"
        if [ -n "$config_compiler" ] && [ "$config_compiler" != "null" ] && [ "$config_compiler" != "" ]; then
            compiler="$config_compiler"
        fi
        
        # For C++ files, use C++ variant of compiler and add C++ flags
        if [[ "$final_source" == *.cpp ]]; then
            if [ "$compiler" = "clang" ]; then
                compiler="clang++"
            elif [ "$compiler" = "gcc" ]; then
                compiler="g++"
            fi
            
            # Add C++ specific flags if defined
            if [ -n "$cpp_flags" ] && [ "$cpp_flags" != "null" ]; then
                final_flags="$special_flags $cpp_flags"
            fi
        fi
        
        echo "$compiler $final_flags $CRITERION_FLAGS -o $final_binary $final_source"
    else
        # Use clang for library and input tests to match normal compilation behavior
        local compiler="clang"
        
        # For C++ files, use C++ variant of compiler and add C++ flags
        if [[ "$final_source" == *.cpp ]]; then
            compiler="clang++"
            
            # Add C++ specific flags if defined
            if [ -n "$cpp_flags" ] && [ "$cpp_flags" != "null" ]; then
                final_flags="$special_flags $cpp_flags"
            fi
        fi
        
        echo "$compiler -o $final_binary $final_source $deps $CRITERION_FLAGS $final_flags"
    fi
    return 0
}

# Function to run test executable with raw output (no shell wrapper)
run_raw_test() {
    local test_binary="$1"
    local test_name="$2"
    
    if [ ! -f "$test_binary" ]; then
        echo "❌ Test binary $test_binary not found"
        return 1
    fi
    
    # For validator tests, run with TAP format to include all test suites
    # and parse the results to provide Synthesis-style summary
    if [[ "$test_binary" == *"validator"* ]]; then
        # Run and capture output to a temporary file to avoid hanging
        local temp_file=$(mktemp)
        ./"$test_binary" --verbose --tap --jobs=$CPU_CORES > "$temp_file" 2>&1
        local exit_code=$?
        
        # Display the output
        cat "$temp_file"
        
        # Parse TAP results for summary
        local total_tests=$(grep -c "^ok " "$temp_file" 2>/dev/null || echo "0")
        local failed_tests=$(grep -c "^not ok " "$temp_file" 2>/dev/null || echo "0")
        
        # Clean numeric values and ensure they're not empty
        total_tests=$(echo "$total_tests" | tr -cd '0-9')
        failed_tests=$(echo "$failed_tests" | tr -cd '0-9')
        total_tests=${total_tests:-0}
        failed_tests=${failed_tests:-0}
        
        local passed_tests=$((total_tests - failed_tests))
        
        # Clean up temp file
        rm -f "$temp_file"
        
        # Provide Synthesis-style summary for consistency
        echo ""
        echo "[====] Final Summary: Tested: $total_tests | Passed: $passed_tests | Failed: $failed_tests | Crashed: 0"
        
        return $exit_code
    else
        # Run the test directly and let it handle its own output
        ./"$test_binary"
    fi
    return $?
}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    if [ "$PIPED_OUTPUT" = true ]; then
        echo -e "${BLUE}$1${NC}" 2>/dev/null || exit 0
    else
        echo -e "${BLUE}$1${NC}"
    fi
}

print_success() {
    if [ "$PIPED_OUTPUT" = true ]; then
        echo -e "${GREEN}✅ $1${NC}" 2>/dev/null || exit 0
    else
        echo -e "${GREEN}✅ $1${NC}"
    fi
}

print_warning() {
    if [ "$PIPED_OUTPUT" = true ]; then
        echo -e "${YELLOW}⚠️  $1${NC}" 2>/dev/null || exit 0
    else
        echo -e "${YELLOW}⚠️  $1${NC}"
    fi
}

print_error() {
    if [ "$PIPED_OUTPUT" = true ]; then
        echo -e "${RED}❌ $1${NC}" 2>/dev/null || exit 0
    else
        echo -e "${RED}❌ $1${NC}"
    fi
}

# Function to load test configuration from JSON
load_test_config() {
    if [ ! -f "$BUILD_CONFIG_FILE" ]; then
        print_error "Build configuration file $BUILD_CONFIG_FILE not found!"
        exit 1
    fi
    
    if ! command -v jq &> /dev/null; then
        print_error "jq is required to parse the configuration file"
        print_error "Please install jq: brew install jq"
        exit 1
    fi
    
    print_status "Loading test configuration from $BUILD_CONFIG_FILE (test section)..."
    
    # Phase 6: Check build prerequisites early in the process
    if ! check_build_prerequisites; then
        print_warning "Build prerequisites check failed. Some tests may not work correctly."
        print_warning "Consider running 'make build' to ensure all object files are current."
        # Don't exit here - allow tests to proceed with fallback mechanisms
    fi
    
    # Load all test suite names (compatible with bash 3.2+)
    TEST_SUITE_NAMES=()
    while IFS= read -r suite_name; do
        TEST_SUITE_NAMES+=("$suite_name")
    done < <(jq -r '.test.test_suites[].suite' "$BUILD_CONFIG_FILE")
    
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
    local env_vars=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_name\") | .environment // {} | to_entries | map(\"\(.key)=\(.value)\") | join(\"|||\")" "$BUILD_CONFIG_FILE" 2>/dev/null || echo "")
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
        "validator"|"library"|"input"|"mir"|"lambda")
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
    
    # Check if we should run in sequential mode due to piped output
    if [ "$PIPED_OUTPUT" = "true" ]; then
        safe_echo "Running tests sequentially due to piped output..."
        
        # Run each test sequentially
        for i in "${!sources_array[@]}"; do
            local test_source="${sources_array[$i]}"
            local test_binary="${binaries_array[$i]}"
            local test_deps="${dependencies_array[$i]}"
            local test_name="${sources_array[$i]%%.c}"
            
            # Add test/ prefix if not already present
            if [[ "$test_source" != test/* ]]; then
                test_source="test/$test_source"
            fi
            if [[ "$test_binary" != test/* ]]; then
                test_binary="test/$test_binary"
            fi
            
            safe_echo "🔧 Compiling $test_source..."
            
            local compile_cmd=""
            
            # Use the same library-based compilation logic as raw mode
            compile_cmd=$(build_library_based_compile_cmd "$suite_name" "$test_source" "$test_binary" "$i" "$special_flags")
            
            if [ $? -ne 0 ]; then
                # Fallback to legacy dependency-based compilation
                compile_cmd=$(build_dependency_based_compile_cmd "$suite_name" "$test_source" "$test_binary" "$test_deps" "$special_flags")
                if [ $? -ne 0 ]; then
                    safe_echo "$compile_cmd"  # This will be the error message
                    total_failed=$((total_failed + 1))
                    continue
                fi
            fi
            
            if $compile_cmd 2>/dev/null; then
                safe_echo "✅ Compiled $test_source successfully"
                
                # Run test sequentially
                safe_echo "🧪 Running $test_binary..."
                
                set +e
                local output=$(./"$test_binary" 2>&1)
                local exit_code=$?
                set -e
                
                safe_echo "$output"
                safe_echo ""
                
                # Parse results - handle both TAP and Criterion output formats
                local test_total=0
                local test_failed=0
                
                # Prefer Criterion Synthesis format when both are present (more accurate)
                if echo "$output" | grep -q "Synthesis:"; then
                    # Criterion synthesis format (only counts executed tests, excludes skipped)
                    local synthesis_line=$(echo "$output" | grep "Synthesis:" | tail -1)
                    test_total=$(echo "$synthesis_line" | grep -o "Tested: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$(echo "$synthesis_line" | grep -o "Failing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    local crashing_tests=$(echo "$synthesis_line" | grep -o "Crashing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$((test_failed + crashing_tests))
                elif echo "$output" | grep -q "^ok "; then
                    # TAP format (includes skipped tests in count)
                    test_total=$(echo "$output" | grep -c "^ok " 2>/dev/null || echo "0")
                    test_failed=$(echo "$output" | grep -c "^not ok " 2>/dev/null || echo "0")
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
                
                test_names+=("$test_name")
                test_totals+=($test_total)
                test_passed+=($test_passed)
                test_failed+=($test_failed)
                
                total_tests=$((total_tests + test_total))
                total_failed=$((total_failed + test_failed))
                
                # Cleanup
                if [ -f "$test_binary" ] && [ "$KEEP_EXE" = false ]; then
                    rm "$test_binary"
                fi
            else
                safe_echo "❌ Failed to compile $test_source"
                $compile_cmd
                total_failed=$((total_failed + 1))
                
                test_names+=("$test_name")
                test_totals+=(0)
                test_passed+=(0)
                test_failed+=(1)
            fi
        done
        
        total_passed=$((total_tests - total_failed))
        
        # Store results using generic approach
        store_suite_results "$suite_name" "$total_tests" "$total_passed" "$total_failed" \
                           test_names test_totals test_passed test_failed
        
        # Print summary for sequential execution
        echo ""
        local suite_display_name=$(get_config "$suite_name" "name")
        print_status "📊 $suite_display_name Results Summary:"
        echo "   Total Tests: $total_tests"
        echo "   Passed: $total_passed"
        echo "   Failed: $total_failed"
        
        # Demonstrate the generic results retrieval API
        display_suite_summary "$suite_name"
        
        if [ $total_failed -eq 0 ] && [ $total_tests -gt 0 ]; then
            print_success "All $suite_display_name tests passed!"
        elif [ $total_tests -eq 0 ]; then
            print_warning "No tests were detected for $suite_display_name"
        else
            print_error "$total_failed $suite_display_name test(s) failed"
        fi
        
        return $total_failed
    fi
    
    # Continue with original parallel execution for non-piped output
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
            # Standard compilation (for library and input tests) - use library-based compilation
            compile_cmd=$(build_library_based_compile_cmd "$suite_name" "$test_source" "$test_binary" "$i" "$special_flags")
            
            if [ $? -ne 0 ]; then
                # Fallback to legacy dependency-based compilation
                compile_cmd=$(build_dependency_based_compile_cmd "$suite_name" "$test_source" "$test_binary" "$test_deps" "$special_flags")
                if [ $? -ne 0 ]; then
                    print_error "$compile_cmd"  # This will be the error message
                    total_failed=$((total_failed + 1))
                    continue
                fi
            fi
        fi
        
        if $compile_cmd 2>/dev/null; then
            print_success "Compiled $test_source successfully"
            
            # Start test in background
            print_status "Starting $test_binary in parallel..."
            (
                set +e
                local output=$(./"$test_binary" 2>&1)
                local exit_code=$?
                set -e
                
                # Parse results - handle both TAP and Criterion output formats
                local test_total=0
                local test_failed=0
                
                # Prefer Criterion Synthesis format when both are present (more accurate)
                if echo "$output" | grep -q "Synthesis:"; then
                    # Criterion synthesis format (only counts executed tests, excludes skipped)
                    local synthesis_line=$(echo "$output" | grep "Synthesis:" | tail -1)
                    test_total=$(echo "$synthesis_line" | grep -o "Tested: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$(echo "$synthesis_line" | grep -o "Failing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    local crashing_tests=$(echo "$synthesis_line" | grep -o "Crashing: [0-9]\+" | grep -o "[0-9]\+" || echo "0")
                    test_failed=$((test_failed + crashing_tests))
                elif echo "$output" | grep -q "^ok "; then
                    # TAP format (includes skipped tests in count)
                    test_total=$(echo "$output" | grep -c "^ok " 2>/dev/null || echo "0")
                    test_failed=$(echo "$output" | grep -c "^not ok " 2>/dev/null || echo "0")
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
                if [ -f "$test_binary" ] && [ "$KEEP_EXE" = false ]; then
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
            safe_echo "$test_output"
            safe_echo ""
            
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
        # Strip extension from test name if present for matching
        local base_test_name="$test_name"
        if [[ "$test_name" == *.c ]]; then
            base_test_name="${test_name%.c}"
        elif [[ "$test_name" == *.cpp ]]; then
            base_test_name="${test_name%.cpp}"
        fi
        
        for i in "${!sources_array[@]}"; do
            if [[ "${sources_array[$i]}" == "test_${base_test_name}.c" ]] || [[ "${sources_array[$i]}" == "test/test_${base_test_name}.cpp" ]] || [[ "${sources_array[$i]}" == "test_${base_test_name}.cpp" ]]; then
                test_index=$i
                break
            fi
        done
        
        if [ $test_index -eq -1 ]; then
            print_error "Test '$test_name' not found in $suite_type tests"
            print_status "Available $suite_type tests:"
            for source_file in "${sources_array[@]}"; do
                if [[ "$source_file" == *.c ]]; then
                    test_basename=$(basename "$source_file" .c)
                elif [[ "$source_file" == *.cpp ]]; then
                    test_basename=$(basename "$source_file" .cpp)
                else
                    test_basename=$(basename "$source_file")
                fi
                # Remove "test_" prefix from basename
                test_name_only="${test_basename#test_}"
                echo "  - ${test_name_only}"
            done
            return 1
        fi
        
        source="${sources_array[$test_index]}"
        binary="${binaries_array[$test_index]}"
        deps="${dependencies_array[$test_index]}"
    fi
    
    # Handle raw mode using generic approach
    if [ "$RAW_OUTPUT" = true ]; then
        local raw_compile_cmd
        local raw_binary_path=""
        
        # Use library-based compilation command builder with test index for library dependency resolution
        raw_compile_cmd=$(build_library_based_compile_cmd "$suite_type" "$source" "$binary" "$test_index" "$special_flags")
        
        if [ $? -ne 0 ]; then
            # Fallback to legacy dependency-based compilation
            raw_compile_cmd=$(build_dependency_based_compile_cmd "$suite_type" "$source" "$binary" "$deps" "$special_flags")
            if [ $? -ne 0 ]; then
                echo "$raw_compile_cmd"  # This will be the error message
                return 1
            fi
        fi
        
        # Determine binary path based on suite type
        if [ "$suite_type" = "validator" ] || [ "$suite_type" = "mir" ] || [ "$suite_type" = "lambda" ]; then
            raw_binary_path="$binary"
        else
            raw_binary_path="test/$binary" 
        fi
        
        # Compile and run
        echo "🔧 Compiling with raw mode: $raw_compile_cmd"
        if $raw_compile_cmd 2>/dev/null; then
            run_raw_test "$raw_binary_path" "$test_name"
            return $?
        else
            echo "❌ Failed to compile $source"
            $raw_compile_cmd  # Show error output
            # Print a minimal synthesis line to indicate compilation failure
            echo "[====] Final Summary: Tested: 0 | Passed: 0 | Failed: 1 | Crashed: 0"
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
        local env_vars=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_type\") | .environment // {} | to_entries | map(\"\(.key)=\(.value)\") | join(\"|||\")" "$BUILD_CONFIG_FILE" 2>/dev/null || echo "")
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
        
        # Use the new library-based compilation system (Phase 5)
        local test_index=-1
        if [ "$suite_type" != "validator" ] && [ "$suite_type" != "mir" ] && [ "$suite_type" != "lambda" ]; then
            # For library and input tests, find the test index
            for i in "${!sources_array[@]}"; do
                if [[ "${sources_array[$i]}" == "test_${test_name}.c" ]]; then
                    test_index=$i
                    break
                fi
            done
        fi
        
        # Use library-based compilation logic with library resolution
        local compile_cmd=$(build_library_based_compile_cmd "$suite_type" "$source" "$binary" "$test_index" "$special_flags")
        if [ $? -ne 0 ]; then
            # Fallback to legacy dependency-based compilation
            compile_cmd=$(build_dependency_based_compile_cmd "$suite_type" "$source" "$binary" "$deps" "$special_flags")
            if [ $? -ne 0 ]; then
                print_error "$compile_cmd"  # This will be the error message
                return 1
            fi
        fi
        
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
    
    safe_echo "$test_output"
    safe_echo ""
    
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
    if [ -f "$binary_path" ] && [ "$KEEP_EXE" = false ]; then
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

# Function to run individual input test
run_individual_input_test() {
    run_individual_test "input" "$1"
    return $?
}

# Generalized function to run a suite in raw mode
run_suite_raw_mode() {
    local suite_name="$1"
    
    # Load configuration if not already loaded
    if [ ${#TEST_SUITE_NAMES[@]} -eq 0 ]; then
        load_test_config
    fi
    
    # Get suite configuration
    local sources_str=$(get_config_array "$suite_name" "sources" " ")
    local suite_display_name=$(get_config "$suite_name" "name")
    
    if [ -z "$sources_str" ]; then
        print_error "No configuration found for suite: $suite_name"
        return 1
    fi
    
    # Parse arrays using bash 3.2 compatible method
    IFS=' ' read -ra sources_array <<< "$sources_str"
    
    print_status "🚀 Running $suite_display_name in RAW mode"
    
    local overall_failed=0
    local total_tested=0
    local total_passed=0
    local total_failed=0
    local total_crashed=0
    local temp_summary_file="/tmp/lambda_test_summary_$$"
    
    # Create temp file for collecting synthesis lines
    > "$temp_summary_file"
    
    # For suites with multiple tests, run each one individually
    if [ ${#sources_array[@]} -gt 1 ]; then
        # For library tests in raw mode, compile and run each test cleanly like lambda mode
        for source_file in "${sources_array[@]}"; do
            # Extract test name from source file
            local test_name=$(basename "$source_file" .c)
            test_name=${test_name#test_}  # Remove "test_" prefix if present
            
            # Run test and capture exit code, but let output flow to terminal
            set +e
            run_individual_test "$suite_name" "$test_name" 2>&1 | tee -a "$temp_summary_file"
            local exit_code=${PIPESTATUS[0]}  # Get exit code of run_individual_test, not tee
            set -e
            
            if [ $exit_code -eq 0 ]; then
                : # Test passed
            else
                overall_failed=$((overall_failed + exit_code))
            fi
        done
    else
        # For suites with single tests, run the suite test directly
        local test_name="$suite_name"
        print_status "Running single suite test: $test_name"
        
        # Run test and capture exit code, but let output flow to terminal
        set +e
        run_individual_test "$suite_name" "$test_name" 2>&1 | tee -a "$temp_summary_file"
        local exit_code=${PIPESTATUS[0]}  # Get exit code of run_individual_test, not tee
        set -e
        
        if [ $exit_code -eq 0 ]; then
            print_success "$test_name test completed successfully"
        else
            print_error "$test_name test failed with exit code: $exit_code"
            overall_failed=$exit_code
        fi
    fi
    
    # Parse all synthesis lines from the accumulated output
    local found_synthesis=false
    while IFS= read -r line; do
        # Check if we have Synthesis format (more accurate - excludes skipped tests)
        if [[ "$line" =~ ^\[====\]\ Synthesis: ]]; then
            found_synthesis=true
            local tested=$(echo "$line" | sed -n 's/.*Tested: \([0-9]*\).*/\1/p')
            local passed=$(echo "$line" | sed -n 's/.*Passing: \([0-9]*\).*/\1/p')
            local failed=$(echo "$line" | sed -n 's/.*Failing: \([0-9]*\).*/\1/p')
            local crashed=$(echo "$line" | sed -n 's/.*Crashing: \([0-9]*\).*/\1/p')
            
            # Ensure we have valid numbers (default to 0 if empty)
            tested=${tested:-0}
            passed=${passed:-0}
            failed=${failed:-0}
            crashed=${crashed:-0}
            
            total_tested=$((total_tested + tested))
            total_passed=$((total_passed + passed))
            total_failed=$((total_failed + failed))
            total_crashed=$((total_crashed + crashed))
        fi
    done < "$temp_summary_file"
    
    # If no Synthesis format found, fall back to Final Summary format (includes skipped tests)
    if [ "$found_synthesis" = false ]; then
        while IFS= read -r line; do
            if [[ "$line" =~ ^\[====\]\ Final\ Summary: ]]; then
                local tested=$(echo "$line" | sed -n 's/.*Tested: \([0-9]*\).*/\1/p')
                local passed=$(echo "$line" | sed -n 's/.*Passed: \([0-9]*\).*/\1/p')
                local failed=$(echo "$line" | sed -n 's/.*Failed: \([0-9]*\).*/\1/p')
                local crashed=$(echo "$line" | sed -n 's/.*Crashed: \([0-9]*\).*/\1/p')
                
                # Ensure we have valid numbers (default to 0 if empty)
                tested=${tested:-0}
                passed=${passed:-0}
                failed=${failed:-0}
                crashed=${crashed:-0}
                
                total_tested=$((total_tested + tested))
                total_passed=$((total_passed + passed))
                total_failed=$((total_failed + failed))
                total_crashed=$((total_crashed + crashed))
            fi
        done < "$temp_summary_file"
    fi
    
    # Clean up temp file
    rm -f "$temp_summary_file"
    
    # Print final summary
    echo
    echo "[====] Final Summary: Tested: $total_tested | Passed: $total_passed | Failed: $total_failed | Crashed: $total_crashed"
    
    return $overall_failed
}

# Generalized function to run a suite in normal mode
run_suite_normal_mode() {
    local suite_name="$1"
    
    case "$suite_name" in
        "library")
            print_status "🚀 Running Library Tests Suite"
            run_library_tests
            return $?
            ;;
        "input")
            print_status "🚀 Running Input Processing Tests Suite"
            run_input_tests
            return $?
            ;;
        "validator")
            print_status "🚀 Running Validator Tests Suite"
            run_validator_tests
            return $?
            ;;
        "mir")
            print_status "🚀 Running MIR JIT Tests Suite"
            run_mir_tests
            return $?
            ;;
        "lambda")
            print_status "🚀 Running Lambda Runtime Tests Suite"
            run_lambda_tests
            return $?
            ;;
        *)
            print_error "Unknown suite: $suite_name"
            return 1
            ;;
    esac
}

# Function to run target test suites
run_target_test() {
    local target="$1"
    
    case "$target" in
        "all")
            # Raw mode not supported for "all" target
            if [ "$RAW_OUTPUT" = true ]; then
                print_error "--raw option is not supported with --target=all"
                print_status "Use --raw with individual tests or suite targets"
                return 1
            fi
            # Run all tests (existing behavior)
            return 0
            ;;
        "library"|"input"|"validator"|"mir"|"lambda")
            if [ "$RAW_OUTPUT" = true ]; then
                # Run suite in raw mode by running individual tests from the suite
                run_suite_raw_mode "$target"
                return $?
            else
                # Run suite normally
                run_suite_normal_mode "$target"
                return $?
            fi
            ;;
        "strbuf"|"strview"|"variable_pool"|"num_stack")
            run_individual_library_test "$target"
            return $?
            ;;
        "mime_detect"|"math"|"markup_roundtrip")
            run_individual_input_test "$target"
            return $?
            ;;
        *)
            print_error "Unknown target: $target"
            print_status "Available targets: all, library, input, validator, mir, lambda, strbuf, strview, variable_pool, num_stack, mime_detect, math, markup_roundtrip"
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

# Check if we should run sequentially due to piped output
if [ "$PIPED_OUTPUT" = "true" ]; then
    safe_echo "🚀 Running all test suites sequentially (piped output detected)..."
    safe_echo ""
    
    # Run each test suite sequentially
    overall_failed=0
    
    # Library tests
    safe_echo "================================================"
    safe_echo "           RUNNING LIBRARY TESTS               "
    safe_echo "================================================"
    if run_library_tests; then
        safe_echo "✅ Library tests completed successfully"
    else
        lib_exit_code=$?
        safe_echo "❌ Library tests failed with exit code: $lib_exit_code"
        overall_failed=$((overall_failed + lib_exit_code))
    fi
    safe_echo ""
    
    # Input Processing tests
    safe_echo "================================================"
    safe_echo "         RUNNING INPUT PROCESSING TESTS        "
    safe_echo "================================================"
    if run_input_tests; then
        safe_echo "✅ Input processing tests completed successfully"
    else
        input_exit_code=$?
        safe_echo "❌ Input processing tests failed with exit code: $input_exit_code"
        overall_failed=$((overall_failed + input_exit_code))
    fi
    safe_echo ""
    
    # MIR JIT tests
    safe_echo "================================================"
    safe_echo "            RUNNING MIR JIT TESTS              "
    safe_echo "================================================"
    if run_mir_tests; then
        safe_echo "✅ MIR JIT tests completed successfully"
    else
        mir_exit_code=$?
        safe_echo "❌ MIR JIT tests failed with exit code: $mir_exit_code"
        overall_failed=$((overall_failed + mir_exit_code))
    fi
    safe_echo ""
    
    # Lambda Runtime tests
    safe_echo "================================================"
    safe_echo "          RUNNING LAMBDA RUNTIME TESTS         "
    safe_echo "================================================"
    if run_lambda_tests; then
        safe_echo "✅ Lambda runtime tests completed successfully"
    else
        lambda_exit_code=$?
        safe_echo "❌ Lambda runtime tests failed with exit code: $lambda_exit_code"
        overall_failed=$((overall_failed + lambda_exit_code))
    fi
    safe_echo ""
    
    # Validator tests
    safe_echo "================================================"
    safe_echo "            RUNNING VALIDATOR TESTS            "
    safe_echo "================================================"
    if run_validator_tests; then
        safe_echo "✅ Validator tests completed successfully"
    else
        validator_exit_code=$?
        safe_echo "❌ Validator tests failed with exit code: $validator_exit_code"
        overall_failed=$((overall_failed + validator_exit_code))
    fi
    safe_echo ""
    
    # Final summary for sequential mode
    safe_echo "================================================"
    safe_echo "             SEQUENTIAL TEST SUMMARY           "
    safe_echo "================================================"
    if [ $overall_failed -eq 0 ]; then
        safe_echo "🎉 All test suites passed!"
    else
        safe_echo "❌ Some test suites failed (total failures: $overall_failed)"
    fi
    
    exit $overall_failed
fi

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

# Keep strict error handling disabled during result collection to ensure we collect all results
# We'll re-enable it after processing all test suite results

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

# Re-enable strict error handling now that we've collected all results
set -e

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
   
    exit 1
fi
