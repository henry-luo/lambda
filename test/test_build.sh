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

# Check prerequisites
check_build_prerequisites() {
    # Check if main build is completed
    if [ ! -f "$PROJECT_ROOT/lambda.exe" ]; then
        echo "Main build not found. Run 'make build' first." >&2
        return 1
    fi
    
    # Check if build directory exists with essential objects
    if [ ! -d "$PROJECT_ROOT/build" ]; then
        echo "Build directory not found. Run 'make build' first." >&2
        return 1
    fi
    
    # Check for essential object files
    local essential_objects=(
        "lambda-eval.o"
        "lambda-mem.o"
        "strbuf.o"
        "hashmap.o"
        "variable.o"
    )
    
    for obj in "${essential_objects[@]}"; do
        if [ ! -f "$PROJECT_ROOT/build/$obj" ]; then
            echo "Essential object file 'build/$obj' not found. Run 'make build' first." >&2
            return 1
        fi
    done
    
    return 0
}

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

# Enhanced library dependency resolution using main build system
resolve_test_library_dependencies() {
    local library_deps_array=("$@")
    
    if [ ! -f "$BUILD_CONFIG_FILE" ] || ! has_jq_support; then
        echo "Error: Build configuration not available" >&2
        return 1
    fi
    
    local resolved_flags=""
    local all_includes=""
    local all_objects=""
    local all_static_libs=""
    local all_dynamic_libs=""
    local all_special_flags=""
    
    # Use main build system's library resolution
    resolved_flags=$(resolve_library_dependencies "${library_deps_array[@]}")
    
    echo "$resolved_flags"
}

# Build test executable using the SAME build process as main build
# This ensures perfect alignment between main and test builds
build_test_executable() {
    local suite_type="$1"
    local source="$2" 
    local binary="$3"
    local test_index="$4"
    
    # Check build prerequisites
    if ! check_build_prerequisites; then
        echo "Build prerequisites check failed. Please run 'make build' first."
        return 1
    fi
    
    # Get library dependencies for this specific test
    local lib_deps_str=""
    if [ -n "$test_index" ]; then
        lib_deps_str=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_type\") | .library_dependencies[$test_index] // [] | join(\" \")" "$BUILD_CONFIG_FILE" 2>/dev/null)
    fi
    
    if [ -z "$lib_deps_str" ] || [ "$lib_deps_str" = "null" ]; then
        echo "Error: No library dependencies found for test $test_index in suite $suite_type" >&2
        return 1
    fi
    
    # Convert to array
    eval "lib_deps_array=($lib_deps_str)"
    
    # Handle prefixed paths
    local final_source="$source"
    local final_binary="$binary"
    
    # Add test/ prefix if not already present
    if [[ "$final_source" != test/* ]]; then
        final_source="test/$final_source"
    fi
    if [[ "$final_binary" != test/* ]]; then
        final_binary="test/$final_binary"
    fi
    
    # Get build configuration - SAME as main build
    local base_flags=""
    local base_warnings=""
    
    # Extract flags from main config (SAME as main build)
    while IFS= read -r flag; do
        [ -n "$flag" ] && base_flags="$base_flags -$flag"
    done < <(get_json_array "flags" "$BUILD_CONFIG_FILE")
    
    # Extract warnings from main config (SAME as main build)
    while IFS= read -r warning; do
        [ -n "$warning" ] && base_warnings="$base_warnings -Werror=$warning"
    done < <(get_json_array "warnings" "$BUILD_CONFIG_FILE")
    
    # Add suite-specific special flags
    local special_flags=$(get_config "$suite_type" "special_flags")
    if [ -n "$special_flags" ] && [ "$special_flags" != "null" ]; then
        base_flags="$base_flags $special_flags"
    fi
    
    # Add C++ specific flags if needed
    if is_cpp_file "$final_source"; then
        local cpp_flags=$(get_config "$suite_type" "cpp_flags")
        if [ -n "$cpp_flags" ] && [ "$cpp_flags" != "null" ]; then
            base_flags="$base_flags $cpp_flags"
        fi
    fi
    
    # Resolve library dependencies using main build system
    local resolved_flags=$(resolve_library_dependencies "${lib_deps_array[@]}")
    
    # Parse resolved flags into components
    local includes=$(echo "$resolved_flags" | grep -o '\-I[^[:space:]]*' | tr '\n' ' ')
    local lib_sources=$(echo "$resolved_flags" | grep -o '[^[:space:]]*\.\(c\|cpp\)' | tr '\n' ' ')
    
    # Check if this test uses object-based dependencies (like lambda-runtime-full or lambda-input-format)
    local uses_runtime_objects=false
    for dep in "${lib_deps_array[@]}"; do
        if [[ "$dep" == "lambda-test-runtime" ]] || [[ "$dep" == "lambda-test-input-full" ]]; then
            uses_runtime_objects=true
            break
        fi
    done
    
    # Extract non-source flags - exclude .o files only for source-based libraries to avoid duplication
    if [ "$uses_runtime_objects" = "true" ]; then
        # Include .o files for runtime tests that depend on pre-built objects
        local libs_and_links=$(echo "$resolved_flags" | sed 's/-I[^[:space:]]*//g' | sed 's/[^[:space:]]*\.[c|cpp][^[:space:]]*//g' | sed 's/  */ /g' | sed 's/^ *//' | sed 's/ *$//')
    else
        # Exclude .o files for source-based libraries to prevent duplication
        local libs_and_links=$(echo "$resolved_flags" | sed 's/-I[^[:space:]]*//g' | sed 's/[^[:space:]]*\.[c|cpp][^[:space:]]*//g' | sed 's/[^[:space:]]*\.o[^[:space:]]*//g' | sed 's/  */ /g' | sed 's/^ *//' | sed 's/ *$//')
    fi
    
    # STEP 1: Compile main test source to object file using SAME process as main build
    local test_obj="${final_binary}.o"
    echo "🔧 Step 1: Compiling test source"
    if ! build_compile_to_object "$final_source" "$test_obj" "$includes" "$base_warnings" "$base_flags" "false"; then
        echo "❌ Failed to compile test source: $final_source"
        return 1
    fi
    
    # STEP 2: Compile any additional sources needed (like test_context.c)
    local additional_objects=""
    if [[ "$final_source" == *"test_markup_roundtrip.cpp" ]] || [[ "$final_source" == *"test_math.c" ]]; then
        echo "🔧 Step 2: Compiling test_context.c"
        if ! build_compile_to_object "test/test_context.c" "test/test_context.o" "$includes" "$base_warnings" "$base_flags" "false"; then
            echo "❌ Failed to compile test_context.c"
            return 1
        fi
        additional_objects="test/test_context.o"
    fi
    
    # STEP 3: Collect object files - reuse existing main build objects for library sources
    local object_files="$test_obj"
    
    # Add pre-built object files from main build for library sources
    for src in $lib_sources; do
        local obj_name=$(basename "$src" | sed 's/\.[^.]*$/.o/')
        local obj_path="build/$obj_name"
        if [ -f "$obj_path" ]; then
            object_files="$object_files $obj_path"
        else
            echo "⚠️  Warning: Object file not found for $src: $obj_path"
            # Fallback: compile the source file using SAME process as main build
            local temp_obj="test/$(basename "$src" | sed 's/\.[^.]*$/.o/')"
            echo "🔧 Compiling missing dependency: $src"
            if ! build_compile_to_object "$src" "$temp_obj" "$includes" "$base_warnings" "$base_flags" "false"; then
                echo "❌ Failed to compile dependency: $src"
                return 1
            fi
            object_files="$object_files $temp_obj"
        fi
    done
    
    # Add additional objects
    if [ -n "$additional_objects" ]; then
        object_files="$object_files $additional_objects"
    fi
    
    # Deduplicate object files to prevent linking errors
    local deduplicated_objects=""
    for obj in $object_files; do
        if [[ "$deduplicated_objects" != *"$obj"* ]]; then
            deduplicated_objects="$deduplicated_objects $obj"
        fi
    done
    object_files=$(echo "$deduplicated_objects" | sed 's/^ *//')

    # STEP 4: Link using SAME process as main build
    echo "🔧 Step 3: Linking executable"
    local force_cxx="false"
    if is_cpp_file "$final_source" || [[ "$object_files" == *".cpp"* ]]; then
        force_cxx="true"
    fi
    
    if ! build_link_executable "$final_binary" "$object_files" "" "$libs_and_links" "" "$force_cxx"; then
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
    
    if ! check_build_prerequisites; then
        echo "❌ Build prerequisites not met"
        return 1
    fi
    
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
        
        # Execute compilations
        for task in "${compilation_data[@]}"; do
            IFS='|' read -r suite_name test_source test_binary index <<< "$task"
            
            echo "🔧 Building $suite_name: $test_source -> $test_binary"
            
            # Execute compilation and capture output
            local compile_output
            if compile_output=$(build_test_executable "$suite_name" "$test_source" "$test_binary" "$index" 2>&1); then
                echo "   ✅ Success"
                successful_builds=$((successful_builds + 1))
            else
                echo "   ❌ Failed"
                failed_builds=$((failed_builds + 1))
                # Accumulate error output for summary
                build_output="$build_output\n$compile_output"
            fi
        done
    fi
    
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
