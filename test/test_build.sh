#!/usr/bin/env bash

# Enhanced Test Build System - Lambda Script Project
# Rewritten to leverage main build infrastructure with unified configuration

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$(basename "$SCRIPT_DIR")" = "test" ]; then
    # Script is in test directory, project root is parent
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
else
    # Script was sourced from project root
    PROJECT_ROOT="$PWD"
fi
BUILD_CONFIG_FILE="$PROJECT_ROOT/build_lambda_config.json"

# Source shared build utilities
if [ -f "$PROJECT_ROOT/utils/build_core.sh" ]; then
    source "$PROJECT_ROOT/utils/build_core.sh"
else
    echo "Error: build_core.sh not found at $PROJECT_ROOT/utils/build_core.sh"
    exit 1
fi

if [ -f "$PROJECT_ROOT/utils/build_utils.sh" ]; then
    source "$PROJECT_ROOT/utils/build_utils.sh"
else
    echo "Warning: build_utils.sh not found at $PROJECT_ROOT/utils/build_utils.sh"
fi

# Prerequisites are ensured by Makefile dependency: build-test depends on build

# Enhanced function to get configuration value with inheritance
get_config() {
    local suite="$1"
    local key="$2"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        echo ""
        return 1
    fi
    
    # Try test suite specific value first, then global fallback
    local suite_value=$(jq -r ".test.test_suites[] | select(.suite == \"$suite\") | .$key // empty" "$BUILD_CONFIG_FILE" 2>/dev/null)
    if [ -n "$suite_value" ] && [ "$suite_value" != "null" ] && [ "$suite_value" != "" ]; then
        echo "$suite_value"
    else
        # Fallback to global config
        jq -r ".$key // empty" "$BUILD_CONFIG_FILE" 2>/dev/null
    fi
}

# Enhanced function to get configuration array
get_config_array() {
    local suite="$1"
    local key="$2"
    local separator="${3:-|||}"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        echo ""
        return 1
    fi
    
    jq -r ".test.test_suites[] | select(.suite == \"$suite\") | .$key | join(\"$separator\")" "$BUILD_CONFIG_FILE" 2>/dev/null || echo ""
}

# Function to get test index for a source file within a test suite
get_test_index_for_source() {
    local suite_type="$1"
    local source_file="$2"
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        echo "0"
        return
    fi
    
    # Extract just the filename from the source path
    local source_filename=$(basename "$source_file")
    
    # Get all sources from the test suite configuration
    local sources=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_type\") | .sources[]" "$BUILD_CONFIG_FILE" 2>/dev/null)
    
    # Find matching index
    local index=0
    while IFS= read -r config_source; do
        # Check for exact match or basename match
        if [ "$config_source" = "$source_file" ] || [ "$(basename "$config_source")" = "$source_filename" ]; then
            echo "$index"
            return
        fi
        index=$((index + 1))
    done <<< "$sources"
    
    # If no match found, return 0
    echo "0"
}

# Enhanced function to build a test executable using unified build functions
build_test_executable() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="${4:-0}"
    
    # Work from the script directory (test/) and resolve relative to repo root
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local repo_root="$(cd "$script_dir/.." && pwd)"
    cd "$repo_root" || return 1
    
    local source_path="$test_source"
    local binary_path="$test_binary"
    
    # Add test/ prefix if not already present
    if [[ "$source_path" != test/* ]]; then
        source_path="test/$source_path"
    fi
    if [[ "$binary_path" != test/* ]]; then
        binary_path="test/$binary_path"
    fi
    
    # Final paths
    local final_source="$source_path"
    local final_binary="$binary_path"
    
    echo "🔧 Building test: $suite_name"
    echo "   Source: $final_source"
    echo "   Binary: $final_binary"
    
    # Ensure we have unified build functions available
    if ! command -v unified_compile_sources >/dev/null 2>&1; then
        echo "🔧 Sourcing unified build functions..."
        source "utils/build_core.sh"
        source "utils/build_utils.sh"
    fi
    
    # Determine test configuration and dependencies based on test index or source path
    local test_config=""
    local library_names=""
    
    if [[ "$final_source" == *"test_validator"* ]] || [[ "$final_source" == *"test_mem"* ]] || [[ "$final_source" == *"test_string"* ]] || [[ "$final_source" == *"test_lambda_data"* ]] || [[ "$final_source" == *"test_ast"* ]] || [[ "$final_source" == *"test_lambda_eval"* ]] || [[ "$final_source" == *"test_mir"* ]] || [[ "$final_source" == *"test_lambda"* ]] || [[ "$final_source" == *"lambda_test_runner"* ]]; then
        # Tests requiring lambda-runtime-full with criterion (includes MIR, Lambda runtime, and validator tests)
        test_config="test-runtime-full"
        library_names="lambda-runtime-full"
    elif [[ "$final_source" == *"test_input"* ]] || [[ "$final_source" == *"test_format"* ]] || [[ "$final_source" == *"test_markup"* ]] || [[ "$final_source" == *"test_math"* ]] || [[ "$final_source" == *"test_mime_detect"* ]]; then
        # Tests requiring lambda-input-full with criterion (all input/format tests)
        test_config="test-input-full"
        library_names="lambda-input-full"
    elif [[ "$final_source" == *"test_datetime"* ]] || [[ "$final_source" == *"test_url"* ]] || [[ "$final_source" == *"test_num_stack"* ]] || [[ "$final_source" == *"test_context"* ]] || [[ "$final_source" == *"test_layout"* ]]; then
        # Tests requiring basic libraries (datetime, url, num_stack, etc.) with criterion
        test_config="test-lib"
        library_names="lambda-lib"
    else
        # Default basic configuration with criterion  
        test_config="test-lib"
        library_names="lambda-lib"
    fi
    
    echo "🎯 Test configuration: $test_config"
    
    # Prepare source files for compilation
    local all_sources="$final_source"
    
    # Add test_context.c for specific tests that need it
    if [[ "$final_source" == *"test_markup_roundtrip.cpp" ]] || [[ "$final_source" == *"test_math.c" ]] || [[ "$final_source" == *"test_mime_detect.c" ]]; then
        all_sources="$all_sources test/test_context.c"
    fi
    
    # STEP 1: Use unified compilation function
    echo "🔧 Step 1: Compiling sources using unified build functions"
    local object_files
    
    # Special handling for lambda_test_runner.cpp - unified system doesn't handle standalone test files
    if [[ "$final_source" == *"lambda_test_runner.cpp"* ]]; then
        local obj_name=$(basename "$final_source" | sed 's/\.[^.]*$//')
        local obj_file="build/${obj_name}.o"
        
        echo "🔧 Compiling standalone test file directly: $final_source"
        clang++ -std=c++17 -fms-extensions -I. -Ilib/mem-pool/include -Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -I/usr/local/include -I/opt/homebrew/Cellar/mpdecimal/4.0.1/include -I/opt/homebrew/include -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -c "$final_source" -o "$obj_file"
        if [ $? -ne 0 ]; then
            echo "❌ Failed to compile $final_source"
            return 1
        fi
        object_files="$obj_file"
    else
        if ! object_files=$(unified_compile_sources "$all_sources" "$library_names" "$repo_root/build_lambda_config.json" "build"); then
            echo "❌ Failed to compile sources for test: $suite_name"
            return 1
        fi
    fi
    
    # STEP 2: Use unified linking function
    echo "🔧 Step 2: Linking executable using unified build functions"
    if ! unified_link_objects "$final_binary" "$object_files" "$library_names" "$repo_root/build_lambda_config.json"; then
        echo "❌ Failed to link executable: $final_binary"
        return 1
    fi
    
    echo "✅ Successfully built: $final_binary"
    return 0
}

# Main build function for individual tests
build_test() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="${4:-}"
    
    # If test_index is not provided, determine it from the source file
    if [ -z "$test_index" ]; then
        test_index=$(get_test_index_for_source "$suite_name" "$test_source")
    fi
    
    # Build test executable using shared build core functions
    if build_test_executable "$suite_name" "$test_source" "$test_binary" "$test_index"; then
        return 0
    else
        echo "❌ Failed to build test executable"
        return 1
    fi
}

# Enhanced function to build all tests with comprehensive reporting
build_all_tests() {
    echo "🔨 Enhanced Test Build System - Building all test executables..."
    echo ""
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        echo "❌ Missing build configuration or jq support"
        return 1
    fi
    
    local total_tests=0
    local successful_builds=0
    local failed_builds=0
    local build_output=""
    local files_compiled=0
    local files_skipped=0
    
    # Parallel job management variables
    local pids=()
    local active_jobs=0
    
    # Set up parallel jobs
    local parallel_jobs=$(setup_parallel_jobs "${PARALLEL_JOBS:-}")
    echo "Parallel jobs: $parallel_jobs"
    
    # Get all test suites from configuration
    local test_suites=$(jq -r '.test.test_suites[].suite' "$BUILD_CONFIG_FILE" 2>/dev/null)
    
    if [ -z "$test_suites" ]; then
        echo "❌ No test suites found in configuration"
        return 1
    fi
    
    echo "📋 Found test suites: $(echo "$test_suites" | tr '\n' ' ')"
    echo ""
    
    # Prepare compilation data for parallel processing
    declare -a compilation_data
    
    # Collect all compilation tasks
    while IFS= read -r suite_name; do
        [ -z "$suite_name" ] && continue
        
        # Get test sources and binaries for this suite
        local test_sources=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_name\") | .sources[]?" "$BUILD_CONFIG_FILE" 2>/dev/null)
        local test_binaries=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_name\") | .binaries[]?" "$BUILD_CONFIG_FILE" 2>/dev/null)
        
        if [ -n "$test_sources" ] && [ -n "$test_binaries" ]; then
            # Convert to arrays
            local sources_array=($test_sources)
            local binaries_array=($test_binaries)
            
            # Process each test in the suite
            for i in "${!sources_array[@]}"; do
                local test_source="${sources_array[$i]}"
                local test_binary="${binaries_array[$i]}"
                
                # Add test/ prefix if needed
                if [[ ! "$test_source" =~ ^test/ ]]; then
                    test_source="test/$test_source"
                fi
                if [[ ! "$test_binary" =~ ^test/ ]]; then
                    test_binary="test/$test_binary"
                fi
                
                if [ -f "$test_source" ]; then
                    total_tests=$((total_tests + 1))
                    
                    # Check if recompilation is needed
                    if needs_recompilation "$test_source" "$test_binary" "${FORCE_REBUILD:-false}"; then
                        # Build test executable directly using shared functions
                        compilation_data+=("$suite_name|$test_source|$test_binary|$i")
                        files_compiled=$((files_compiled + 1))
                    else
                        echo "Up-to-date: $test_source"
                        files_skipped=$((files_skipped + 1))
                        successful_builds=$((successful_builds + 1))
                    fi
                else
                    echo "⚠️  Skipping $test_source (source file not found)"
                fi
            done
        else
            echo "⚠️  No sources/binaries found for suite: $suite_name"
        fi
    done <<< "$test_suites"
    
    # Perform parallel compilation if needed
    if [ ${#compilation_data[@]} -gt 0 ]; then
        echo "Compiling ${#compilation_data[@]} test files..."
        
        # Execute compilations in parallel using background processes
        local task_outputs=()
        local task_results=()
        local max_jobs=$parallel_jobs
        
        # Start all background jobs with controlled parallelism
        for i in "${!compilation_data[@]}"; do
            local task="${compilation_data[$i]}"
            IFS='|' read -r suite_name test_source test_binary index <<< "$task"
            
            echo "🔧 Building $suite_name: $test_source -> $test_binary"
            
            # Create unique output files for this job
            local output_file="/tmp/test_build_$$_$i"
            local result_file="/tmp/test_build_$$_$i.result"
            task_outputs+=("$output_file")
            task_results+=("$result_file")
            
            # Wait if we've reached the max parallel jobs
            while [ $active_jobs -ge $max_jobs ]; do
                # Simple wait - check for any completed job
                local job_completed=false
                for pid in "${pids[@]}"; do
                    if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
                        # Job completed
                        active_jobs=$((active_jobs - 1))
                        job_completed=true
                        break
                    fi
                done
                
                if [ "$job_completed" = false ]; then
                    sleep 0.1
                fi
            done
            
            # Start compilation in background
            {
                if build_test_executable "$suite_name" "$test_source" "$test_binary" "$index" >"$output_file" 2>&1; then
                    echo "SUCCESS|$suite_name|$test_source|$test_binary" > "$result_file"
                else
                    echo "FAILED|$suite_name|$test_source|$test_binary" > "$result_file"
                fi
            } &
            
            local pid=$!
            pids+=($pid)
            active_jobs=$((active_jobs + 1))
        done
        
        # Wait for all remaining jobs to complete
        echo "Waiting for all test builds to complete..."
        for pid in "${pids[@]}"; do
            if [ -n "$pid" ]; then
                wait $pid
            fi
        done
        
        # Process results
        for i in "${!task_results[@]}"; do
            local result_file="${task_results[$i]}"
            local output_file="${task_outputs[$i]}"
            
            if [ -f "$result_file" ]; then
                local result_line=$(cat "$result_file")
                IFS='|' read -r status suite_name test_source test_binary <<< "$result_line"
                
                if [ "$status" = "SUCCESS" ]; then
                    echo "   ✅ Success: $suite_name/$test_source"
                    successful_builds=$((successful_builds + 1))
                else
                    echo "   ❌ Failed: $suite_name/$test_source"
                    failed_builds=$((failed_builds + 1))
                    # Accumulate error output for summary
                    if [ -f "$output_file" ]; then
                        build_output="$build_output\n$(cat "$output_file")"
                    fi
                fi
                
                # Clean up temporary files
                rm -f "$result_file" "$output_file"
            else
                echo "   ❌ Failed: Missing result file for task $i"
                failed_builds=$((failed_builds + 1))
            fi
        done
    fi

wait_for_job() {
    # Wait for any job to complete
    local completed=false
    while [ "$completed" = false ]; do
        for i in "${!pids[@]}"; do
            local pid="${pids[$i]}"
            if ! kill -0 "$pid" 2>/dev/null; then
                # Job completed, remove from active list
                unset pids[$i]
                pids=("${pids[@]}")  # Re-index array
                active_jobs=$((active_jobs - 1))
                completed=true
                break
            fi
        done
        
        if [ "$completed" = false ]; then
            sleep 0.1
        fi
    done
}
    
    # Generate comprehensive build summary
    echo ""
    echo "=========================================="
    if generate_build_summary "$build_output" "$files_compiled" "$files_skipped" "false" "build" "test"; then
        echo -e "${GREEN}✅ Test Build Summary:${RESET}"
        echo -e "   Total tests: $total_tests"
        echo -e "   ✅ Successful: $successful_builds"
        echo -e "   ❌ Failed: $failed_builds"
        
        if [ $failed_builds -eq 0 ]; then
            echo -e "${GREEN}🎉 All tests built successfully!${RESET}"
            return 0
        else
            echo -e "${YELLOW}⚠️  Some tests failed to build${RESET}"
            return 1
        fi
    else
        echo -e "${RED}❌ Test build completed with errors${RESET}"
        return 1
    fi
}

# Simplified build function for compatibility
build_test_simple() {
    build_test "$@"
}

# If script is run directly with "all" argument, build all tests
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    if [ "$1" = "all" ]; then
        build_all_tests
    elif [ $# -eq 2 ]; then
        # Single test build: suite_type source_file
        suite_type="$1"
        source_file="$2"
        binary_name=$(basename "$source_file" | sed 's/\.[^.]*$//').exe
        build_test "$suite_type" "$source_file" "$binary_name"
    else
        echo "Usage: $0 all                              # Build all tests"
        echo "       $0 <suite_type> <source_file>      # Build single test"
        echo ""
        echo "Examples:"
        echo "  $0 all"
        echo "  $0 library test_strbuf.c"
        echo "  $0 library test_hashmap.cpp"
        exit 1
    fi
fi
