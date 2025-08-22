#!/usr/bin/env bash

# Test Build Utilities - Self-contained compilation functions
# This script provides compilation functions for the enhanced test runner

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_CONFIG_FILE="$SCRIPT_DIR/build_lambda_config.json"

# Source shared build utilities
if [ -f "$SCRIPT_DIR/utils/build_utils.sh" ]; then
    source "$SCRIPT_DIR/utils/build_utils.sh"
else
    echo "Warning: utils/build_utils.sh not found"
fi

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

# Helper function to build compilation command using library dependency resolution
build_library_based_compile_cmd() {
    local suite_type="$1"
    local source="$2" 
    local binary="$3"
    local test_index="$4"
    local special_flags="$5"
    
    # Check build prerequisites before proceeding
    if ! check_build_prerequisites; then
        echo "Build prerequisites check failed. Please run 'make build' first."
        return 1
    fi
    
    # Try to get library dependencies first
    local lib_deps_str=$(get_library_dependencies_for_test "$suite_type" "$test_index")
    
    if [ -n "$lib_deps_str" ]; then
        # Enhanced library name resolution with object file optimization
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
        
        # Use optimized object file approach when possible
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
                # Get include flags and static libraries from library resolution
                local resolved_flags=$(resolve_library_dependencies "${lib_deps_array[@]}")
                
                # Extract criterion flags separately
                local criterion_flags=""
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
                local static_libs=$(echo "$resolved_flags" | grep -o '\S*\.a\s*' | tr '\n' ' ')
                local include_flags=$(echo "$resolved_flags" | grep -o '\-I[^[:space:]]*' | tr '\n' ' ')
                local link_flags=$(echo "$resolved_flags" | grep -o '\-L[^[:space:]]*\s*\-l[^[:space:]]*' | tr '\n' ' ')
                
                # Add utf8proc library for unicode support 
                local utf8proc_libs=""
                for lib_name in "${lib_deps_array[@]}"; do
                    if [[ "$lib_name" == *"input"* ]] || [[ "$lib_name" == *"lambda-core"* ]] || [[ "$lib_name" == *"lambda-runtime-full"* ]] || [[ "$lib_name" == *"lambda-input-core"* ]]; then
                        utf8proc_libs="/opt/homebrew/lib/libutf8proc.a"
                        break
                    fi
                done
                
                echo "$compiler $final_flags $include_flags -o $final_binary $final_source $minimal_objects $static_libs $link_flags $utf8proc_libs $criterion_flags"
                return 0
            fi
        fi
        
        # Fallback to legacy library resolution if object approach fails
        local resolved_flags=$(resolve_library_dependencies "${lib_deps_array[@]}")
        
        echo "$compiler $final_flags -o $final_binary $final_source $resolved_flags"
        return 0
    else
        # Fallback to legacy dependency strings if library_dependencies not available
        return 1
    fi
}

# Function to get test suite configuration arrays (for enhanced test runner compatibility)
get_test_config_array() {
    get_config_array "$@"
}

# Simplified compilation function for enhanced test runner
build_test_simple() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    
    echo "🔧 Building $test_source -> $test_binary"
    
    # Add test/ prefix if not already present
    if [[ "$test_source" != test/* ]]; then
        test_source="test/$test_source"
    fi
    if [[ "$test_binary" != test/* ]]; then
        test_binary="test/$test_binary"
    fi
    
    # Get special flags from config
    local special_flags=$(get_config "$suite_name" "special_flags")
    
    # Get compiler from config
    local compiler=$(get_config "$suite_name" "compiler")
    if [ -z "$compiler" ] || [ "$compiler" = "null" ]; then
        compiler="clang"
    fi
    
    # Get C++ flags if needed
    local cpp_flags=$(get_config "$suite_name" "cpp_flags")
    local final_flags="$special_flags"
    
    # For C++ files, adjust compiler and flags
    if [[ "$test_source" == *.cpp ]]; then
        if [ "$compiler" = "clang" ]; then
            compiler="clang++"
        elif [ "$compiler" = "gcc" ]; then
            compiler="g++"
        fi
        
        if [ -n "$cpp_flags" ] && [ "$cpp_flags" != "null" ]; then
            final_flags="$special_flags $cpp_flags"
        fi
    fi
    
    # Find Criterion flags
    local criterion_flags=""
    if pkg-config --exists criterion 2>/dev/null; then
        criterion_flags=$(pkg-config --cflags --libs criterion)
    elif [ -d "/opt/homebrew/include/criterion" ]; then
        criterion_flags="-I/opt/homebrew/include -L/opt/homebrew/lib -lcriterion"
    else
        echo "❌ Criterion not found" >&2
        return 1
    fi
    
    # Simple compilation command
    local compile_cmd="$compiler $final_flags $criterion_flags -o $test_binary $test_source"
    
    echo "   Command: $compile_cmd"
    if $compile_cmd 2>/dev/null; then
        echo "✅ Compiled successfully"
        return 0
    else
        echo "❌ Compilation failed" >&2
        # Show error output
        $compile_cmd
        return 1
    fi
}

# Main build function that tries the library-based approach first, then falls back to simple
build_test() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="${4:-0}"
    
    # Get special flags from config
    local special_flags=$(get_config "$suite_name" "special_flags")
    
    # Use the exact same compilation command builder from test_all.sh
    local compile_cmd=$(build_library_based_compile_cmd "$suite_name" "$test_source" "$test_binary" "$test_index" "$special_flags")
    
    if [ $? -eq 0 ] && [ -n "$compile_cmd" ]; then
        echo "🔧 Building $test_source -> $test_binary"
        echo "   Command: $compile_cmd"
        if $compile_cmd; then
            echo "✅ Compiled successfully"
            return 0
        else
            echo "❌ Compilation failed"
            return 1
        fi
    else
        echo "❌ Could not build compilation command for $test_source"
        return 1
    fi
}
