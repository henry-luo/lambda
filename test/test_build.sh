#!/usr/bin/env bash

# Test Build Utilities - Uses test_all.sh compilation logic
# This script provides compilation functions for the enhanced test runner

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_CONFIG_FILE="$SCRIPT_DIR/build_lambda_config.json"

# Prevent test_all.sh main execution when sourcing
TEST_ALL_SOURCED=true

# Source the entire test_all.sh to get all its functions, but prevent main execution
if [ -f "$SCRIPT_DIR/test/test_all.sh" ]; then
    # Save the current TARGET_TEST value and set it to something that prevents execution
    ORIGINAL_TARGET_TEST="${TARGET_TEST:-}"
    TARGET_TEST="__SOURCED_NO_EXEC__"
    
    # Save current arguments and clear them to prevent test_all.sh from parsing our arguments
    SAVED_ARGS=("$@")
    set --
    
    # Source test_all.sh
    source "$SCRIPT_DIR/test/test_all.sh" 2>/dev/null
    
    # Restore TARGET_TEST and arguments
    TARGET_TEST="$ORIGINAL_TARGET_TEST"
    set -- "${SAVED_ARGS[@]}"
fi

# Function to get test suite configuration arrays (for enhanced test runner compatibility)
get_test_config_array() {
    get_config_array "$@"
}

# Main build function that uses the exact same compilation logic as test_all.sh
build_test() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="${4:-0}"
    
    # Get special flags from config (using test_all.sh functions)
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

# Simplified functions for backward compatibility
build_test_simple() {
    build_test "$@"
}

build_test_advanced() {
    build_test "$@"
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
    local special_flags=$(get_test_config "$suite_name" "special_flags")
    
    # Get compiler from config
    local compiler=$(get_test_config "$suite_name" "compiler")
    if [ -z "$compiler" ] || [ "$compiler" = "null" ]; then
        compiler="clang"
    fi
    
    # Get C++ flags if needed
    local cpp_flags=$(get_test_config "$suite_name" "cpp_flags")
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

# Function to extract object files from a library configuration
extract_library_objects() {
    local lib_name="$1"
    
    # Get the library configuration from build config
    local lib_config=$(jq -r ".libraries[] | select(.name == \"$lib_name\")" "$BUILD_CONFIG_FILE" 2>/dev/null)
    
    if [ -z "$lib_config" ] || [ "$lib_config" = "null" ]; then
        return 1
    fi
    
    # Extract object files if they exist
    local objects=$(echo "$lib_config" | jq -r '.objects[]?' 2>/dev/null | tr '\n' ' ')
    
    if [ -n "$objects" ]; then
        echo "$objects"
        return 0
    fi
    
    return 1
}

# Function to get static libraries from a library configuration
get_library_static_libs() {
    local lib_name="$1"
    
    # Get the library configuration from build config
    local lib_config=$(jq -r ".libraries[] | select(.name == \"$lib_name\")" "$BUILD_CONFIG_FILE" 2>/dev/null)
    
    if [ -z "$lib_config" ] || [ "$lib_config" = "null" ]; then
        return 1
    fi
    
    # Extract lib path if it's a static library
    local lib_path=$(echo "$lib_config" | jq -r '.lib // empty' 2>/dev/null)
    local link_type=$(echo "$lib_config" | jq -r '.link // empty' 2>/dev/null)
    
    if [ -n "$lib_path" ] && [ "$link_type" = "static" ] && [[ "$lib_path" == *.a ]]; then
        echo "$lib_path"
        return 0
    fi
    
    return 1
}

# Function to get include paths from a library configuration
get_library_includes() {
    local lib_name="$1"
    
    # Get the library configuration from build config
    local lib_config=$(jq -r ".libraries[] | select(.name == \"$lib_name\")" "$BUILD_CONFIG_FILE" 2>/dev/null)
    
    if [ -z "$lib_config" ] || [ "$lib_config" = "null" ]; then
        return 1
    fi
    
    # Extract include path
    local include_path=$(echo "$lib_config" | jq -r '.include // empty' 2>/dev/null)
    
    if [ -n "$include_path" ]; then
        echo "-I$include_path"
        return 0
    fi
    
    return 1
}

# Advanced compilation function using library dependencies
build_test_advanced() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="$4"
    
    echo "🔧 Building $test_source -> $test_binary (advanced)"
    
    # Add test/ prefix if not already present
    if [[ "$test_source" != test/* ]]; then
        test_source="test/$test_source"
    fi
    if [[ "$test_binary" != test/* ]]; then
        test_binary="test/$test_binary"
    fi
    
    # Check if we have library dependencies
    local lib_deps_str=$(get_library_dependencies_for_test "$suite_name" "$test_index")
    
    if [ -z "$lib_deps_str" ]; then
        # No library dependencies, use simple compilation
        build_test_simple "$suite_name" "$test_source" "$test_binary"
        return $?
    fi
    
    # Get configuration
    local special_flags=$(get_test_config "$suite_name" "special_flags")
    local compiler=$(get_test_config "$suite_name" "compiler")
    if [ -z "$compiler" ] || [ "$compiler" = "null" ]; then
        compiler="clang"
    fi
    
    # Get C++ flags if needed
    local cpp_flags=$(get_test_config "$suite_name" "cpp_flags")
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
    
    # Parse library dependencies
    local lib_deps_array=($lib_deps_str)
    local criterion_flags=""
    local object_files=""
    local static_libs=""
    local include_flags=""
    
    # Process each library dependency
    for lib_name in "${lib_deps_array[@]}"; do
        if [ "$lib_name" = "criterion" ]; then
            if pkg-config --exists criterion 2>/dev/null; then
                criterion_flags=$(pkg-config --cflags --libs criterion)
            else
                criterion_flags="-I/opt/homebrew/include -L/opt/homebrew/lib -lcriterion"
            fi
        else
            # Extract object files from library
            local lib_objects=$(extract_library_objects "$lib_name")
            if [ -n "$lib_objects" ]; then
                object_files="$object_files $lib_objects"
            fi
            
            # Extract static libraries
            local lib_static=$(get_library_static_libs "$lib_name")
            if [ -n "$lib_static" ]; then
                static_libs="$static_libs $lib_static"
            fi
            
            # Extract include paths
            local lib_includes=$(get_library_includes "$lib_name")
            if [ -n "$lib_includes" ]; then
                include_flags="$include_flags $lib_includes"
            fi
        fi
    done
    
    # Add utf8proc library for lambda-runtime-full dependencies
    for lib_name in "${lib_deps_array[@]}"; do
        if [[ "$lib_name" == *"lambda-runtime-full"* ]]; then
            static_libs="$static_libs /opt/homebrew/lib/libutf8proc.a"
            break
        fi
    done
    
    # Build compilation command
    local compile_cmd="$compiler $final_flags $include_flags $criterion_flags -o $test_binary $test_source $object_files $static_libs"
    
    echo "   Command: $compile_cmd"
    if $compile_cmd 2>/dev/null; then
        echo "✅ Compiled successfully"
        return 0
    else
        echo "❌ Advanced compilation failed" >&2
        # Show the error
        $compile_cmd
        return 1
    fi
}

# Main build function that tries advanced first, then falls back to simple
build_test() {
    local suite_name="$1"
    local test_source="$2"
    local test_binary="$3"
    local test_index="${4:-0}"
    
    # Try advanced compilation first
    if build_test_advanced "$suite_name" "$test_source" "$test_binary" "$test_index" 2>/dev/null; then
        return 0
    else
        # Fall back to simple compilation
        echo "   Advanced compilation failed, trying simple compilation..."
        build_test_simple "$suite_name" "$test_source" "$test_binary"
        return $?
    fi
}
