#!/bin/bash

# Shared Build Utilities for Lambda Project
# Common functions used by both compile.sh and test_all.sh

# Function to extract values from JSON using jq or basic parsing
get_json_value() {
    local key="$1"
    local file="$2"
    local platform_prefix="$3"
    
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific value first, fallback to default
            local platform_value=$(jq -r ".platforms.$platform_prefix.$key // empty" "$file" 2>/dev/null)
            if [ -n "$platform_value" ] && [ "$platform_value" != "null" ] && [ "$platform_value" != "" ]; then
                echo "$platform_value"
            else
                jq -r ".$key // empty" "$file" 2>/dev/null
            fi
        else
            jq -r ".$key // empty" "$file" 2>/dev/null
        fi
    else
        # Fallback to grep/sed for basic JSON parsing
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific first
            local platform_result=$(grep -A 20 "\"platforms\"" "$file" | grep -A 10 "\"$platform_prefix\"" | grep "\"$key\"" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g')
            if [ -n "$platform_result" ]; then
                echo "$platform_result"
                return
            fi
        fi
        grep "\"$key\"" "$file" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g'
    fi
}

# Function to extract array values from JSON
get_json_array() {
    local key="$1"
    local file="$2"
    local platform_prefix="$3"
    
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific libraries first, fallback to default
            local platform_result=$(jq -r ".platforms.$platform_prefix.$key[]? // empty" "$file" 2>/dev/null)
            if [ -n "$platform_result" ]; then
                echo "$platform_result"
            else
                jq -r ".$key[]? // empty" "$file" 2>/dev/null
            fi
        else
            jq -r ".$key[]? // empty" "$file" 2>/dev/null
        fi
    else
        # Fallback parsing for arrays
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific first
            local platform_section=$(sed -n '/"platforms"/,/}$/p' "$file" | sed -n '/"'$platform_prefix'"/,/}$/p')
            if [ -n "$platform_section" ]; then
                local platform_array=$(echo "$platform_section" | sed -n '/"'$key'":/,/]/p' | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key")
                if [ -n "$platform_array" ]; then
                    echo "$platform_array"
                    return
                fi
            fi
        fi
        sed -n '/"'$key'":/,/]/p' "$file" | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
    fi
}

# Function to resolve library configuration
resolve_library_config() {
    local config_file="$1"
    local platform="$2"
    local lib_name="$3"
    
    if command -v jq >/dev/null 2>&1; then
        # Try platform-specific libraries first
        if [ -n "$platform" ]; then
            local platform_lib=$(jq -r ".platforms.$platform.libraries[]? | select(.name == \"$lib_name\")" "$config_file" 2>/dev/null)
            if [ -n "$platform_lib" ] && [ "$platform_lib" != "null" ]; then
                echo "$platform_lib"
                return
            fi
        fi
        
        # Fallback to default libraries
        jq -r ".libraries[]? | select(.name == \"$lib_name\")" "$config_file" 2>/dev/null
    else
        # Basic parsing fallback
        echo "Error: jq required for library resolution" >&2
        return 1
    fi
}

# Function to collect include flags from libraries
collect_include_flags() {
    local config_file="$1"
    local platform="$2"
    shift 2
    local library_names=("$@")
    
    local include_flags=""
    
    for lib_name in "${library_names[@]}"; do
        local lib_config=$(resolve_library_config "$config_file" "$platform" "$lib_name")
        if [ -n "$lib_config" ] && [ "$lib_config" != "null" ]; then
            local include=$(echo "$lib_config" | jq -r '.include // empty' 2>/dev/null)
            if [ -n "$include" ] && [ "$include" != "null" ]; then
                include_flags="$include_flags -I$include"
            fi
        fi
    done
    
    echo "$include_flags"
}

# Function to collect static library files
collect_static_libs() {
    local config_file="$1"
    local platform="$2"
    shift 2
    local library_names=("$@")
    
    local static_libs=""
    
    for lib_name in "${library_names[@]}"; do
        local lib_config=$(resolve_library_config "$config_file" "$platform" "$lib_name")
        if [ -n "$lib_config" ] && [ "$lib_config" != "null" ]; then
            local link=$(echo "$lib_config" | jq -r '.link // "static"' 2>/dev/null)
            if [ "$link" = "static" ]; then
                local lib=$(echo "$lib_config" | jq -r '.lib // empty' 2>/dev/null)
                if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                    static_libs="$static_libs $lib"
                fi
            fi
        fi
    done
    
    echo "$static_libs"
}

# Function to collect dynamic library flags
collect_dynamic_libs() {
    local config_file="$1"
    local platform="$2"
    shift 2
    local library_names=("$@")
    
    local dynamic_libs=""
    
    for lib_name in "${library_names[@]}"; do
        local lib_config=$(resolve_library_config "$config_file" "$platform" "$lib_name")
        if [ -n "$lib_config" ] && [ "$lib_config" != "null" ]; then
            local link=$(echo "$lib_config" | jq -r '.link // "static"' 2>/dev/null)
            if [ "$link" = "dynamic" ]; then
                local lib=$(echo "$lib_config" | jq -r '.lib // empty' 2>/dev/null)
                if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                    dynamic_libs="$dynamic_libs -L$lib -l$lib_name"
                fi
            fi
        fi
    done
    
    echo "$dynamic_libs"
}

# Function to collect source files for inline libraries
collect_inline_sources() {
    local config_file="$1"
    local platform="$2"
    shift 2
    local library_names=("$@")
    
    local source_files=""
    
    for lib_name in "${library_names[@]}"; do
        local lib_config=$(resolve_library_config "$config_file" "$platform" "$lib_name")
        if [ -n "$lib_config" ] && [ "$lib_config" != "null" ]; then
            local link=$(echo "$lib_config" | jq -r '.link // "static"' 2>/dev/null)
            if [ "$link" = "inline" ]; then
                local sources=$(echo "$lib_config" | jq -r '.sources[]? // empty' 2>/dev/null)
                if [ -n "$sources" ]; then
                    source_files="$source_files $sources"
                fi
            fi
        fi
    done
    
    echo "$source_files"
}

# Function to collect object files based on library dependencies
collect_required_objects() {
    local build_dir="$1"
    shift
    local library_names=("$@")
    
    local object_files=()
    
    for lib_name in "${library_names[@]}"; do
        case "$lib_name" in
            "strbuf")
                [ -f "$build_dir/strbuf.o" ] && object_files+=("$build_dir/strbuf.o")
                ;;
            "strview")
                [ -f "$build_dir/strview.o" ] && object_files+=("$build_dir/strview.o")
                ;;
            "mem-pool")
                [ -f "$build_dir/variable.o" ] && object_files+=("$build_dir/variable.o")
                [ -f "$build_dir/buffer.o" ] && object_files+=("$build_dir/buffer.o")
                [ -f "$build_dir/utils.o" ] && object_files+=("$build_dir/utils.o")
                ;;
            "num_stack")
                [ -f "$build_dir/num_stack.o" ] && object_files+=("$build_dir/num_stack.o")
                ;;
            "datetime")
                [ -f "$build_dir/datetime.o" ] && object_files+=("$build_dir/datetime.o")
                ;;
            "string")
                [ -f "$build_dir/string.o" ] && object_files+=("$build_dir/string.o")
                ;;
            "input")
                # Collect all input-related object files
                for obj in "$build_dir"/input*.o; do
                    [ -f "$obj" ] && object_files+=("$obj")
                done
                [ -f "$build_dir/mime-detect.o" ] && object_files+=("$build_dir/mime-detect.o")
                [ -f "$build_dir/mime-types.o" ] && object_files+=("$build_dir/mime-types.o")
                ;;
            "format")
                # Collect all format-related object files
                for obj in "$build_dir"/format*.o; do
                    [ -f "$obj" ] && object_files+=("$obj")
                done
                ;;
            "lambda-core")
                # Core lambda runtime objects (excluding main.o for tests)
                [ -f "$build_dir/lambda-eval.o" ] && object_files+=("$build_dir/lambda-eval.o")
                [ -f "$build_dir/lambda-mem.o" ] && object_files+=("$build_dir/lambda-mem.o")
                [ -f "$build_dir/transpile.o" ] && object_files+=("$build_dir/transpile.o")
                [ -f "$build_dir/build_ast.o" ] && object_files+=("$build_dir/build_ast.o")
                [ -f "$build_dir/runner.o" ] && object_files+=("$build_dir/runner.o")
                [ -f "$build_dir/parse.o" ] && object_files+=("$build_dir/parse.o")
                [ -f "$build_dir/parser.o" ] && object_files+=("$build_dir/parser.o")
                [ -f "$build_dir/print.o" ] && object_files+=("$build_dir/print.o")
                [ -f "$build_dir/mir.o" ] && object_files+=("$build_dir/mir.o")
                [ -f "$build_dir/url.o" ] && object_files+=("$build_dir/url.o")
                [ -f "$build_dir/utf.o" ] && object_files+=("$build_dir/utf.o")
                [ -f "$build_dir/unicode_string.o" ] && object_files+=("$build_dir/unicode_string.o")
                ;;
        esac
    done
    
    # Remove duplicates and return as space-separated string
    printf '%s\n' "${object_files[@]}" | sort -u | tr '\n' ' '
}

# Function to get automatic test dependencies based on test file name
get_automatic_test_dependencies() {
    local test_file="$1"
    local test_base=$(basename "$test_file" .c)
    
    case "$test_base" in
        "test_strbuf")
            echo "strbuf mem-pool"
            ;;
        "test_strview")
            echo "strview"
            ;;
        "test_variable_pool")
            echo "mem-pool"
            ;;
        "test_num_stack")
            echo "num_stack"
            ;;
        "test_datetime")
            echo "datetime string strbuf strview mem-pool"
            ;;
        "test_mime_detect")
            echo "input"
            ;;
        "test_math")
            echo "lambda-core input format strbuf strview mem-pool datetime string"
            ;;
        "test_markup_roundtrip")
            echo "lambda-core input format strbuf strview mem-pool datetime string"
            ;;
        "test_mir")
            echo "lambda-core"
            ;;
        "test_lambda")
            echo "lambda-core"
            ;;
        *)
            # Default fallback - return basic dependencies
            echo "strbuf mem-pool"
            ;;
    esac
}

# Function to check if we're running with jq support
has_jq_support() {
    command -v jq >/dev/null 2>&1
}

# Function to validate configuration file
validate_config_file() {
    local config_file="$1"
    
    if [ ! -f "$config_file" ]; then
        echo "Error: Configuration file '$config_file' not found!" >&2
        return 1
    fi
    
    if has_jq_support; then
        if ! jq . "$config_file" >/dev/null 2>&1; then
            echo "Error: Invalid JSON in configuration file '$config_file'" >&2
            return 1
        fi
    fi
    
    return 0
}

# Function to resolve library dependencies from test config to actual compile flags
resolve_library_dependencies() {
    local library_deps_array=("$@")
    local build_config_file="${BUILD_CONFIG_FILE:-build_lambda_config.json}"
    local platform=""
    
    local resolved_flags=""
    local include_flags=""
    local source_files=""
    local object_files=""
    local static_libs=""
    local dynamic_libs=""
    
    for lib_name in "${library_deps_array[@]}"; do
        case "$lib_name" in
            "strbuf")
                source_files="$source_files lib/strbuf.c"
                ;;
            "strview")
                source_files="$source_files lib/strview.c"
                ;;
            "mem-pool")
                source_files="$source_files lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c"
                include_flags="$include_flags -Ilib/mem-pool/include"
                ;;
            "num_stack")
                source_files="$source_files lib/num_stack.c"
                ;;
            "datetime")
                source_files="$source_files lib/datetime.c"
                ;;
            "string")
                source_files="$source_files lib/string.c"
                ;;
            "mime-detect")
                source_files="$source_files lambda/input/mime-detect.c lambda/input/mime-types.c"
                ;;
            "lambda-runtime-full")
                # Complex runtime dependencies - use pre-built objects
                source_files="$source_files lib/file.c"
                object_files="$object_files build/print.o build/strview.o build/transpile.o build/utf.o build/build_ast.o build/lambda-eval.o build/lambda-mem.o build/runner.o build/mir.o build/url.o build/parse.o build/parser.o build/num_stack.o build/input*.o build/format*.o build/strbuf.o build/hashmap.o build/arraylist.o build/variable.o build/buffer.o build/utils.o build/mime-detect.o build/mime-types.o build/datetime.o build/string.o build/unicode_string.o"
                static_libs="$static_libs lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a"
                include_flags="$include_flags -Ilib/mem-pool/include"
                dynamic_libs="$dynamic_libs -L/opt/homebrew/lib -lgmp -L/Users/henryluo/Projects/Jubily/icu-compact/lib -licui18n -licuuc -licudata"
                ;;
            "criterion")
                include_flags="$include_flags -I/opt/homebrew/Cellar/criterion/2.4.2_2/include"
                dynamic_libs="$dynamic_libs -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
                ;;
        esac
    done
    
    # Build final resolved flags string
    resolved_flags="$include_flags $source_files $object_files $static_libs $dynamic_libs"
    echo "$resolved_flags"
}

# Function to get library dependencies array from test config
get_library_dependencies_for_test() {
    local suite_name="$1"
    local test_index="$2"
    local build_config_file="${BUILD_CONFIG_FILE:-build_lambda_config.json}"
    
    if [ ! -f "$build_config_file" ]; then
        echo ""
        return 1
    fi
    
    if has_jq_support; then
        # Get the library dependencies array for the specific test index
        local lib_deps=$(jq -r ".test.test_suites[] | select(.suite == \"$suite_name\") | .library_dependencies[$test_index] // empty" "$build_config_file" 2>/dev/null)
        if [ -n "$lib_deps" ] && [ "$lib_deps" != "null" ]; then
            # Parse JSON array and return as space-separated string
            echo "$lib_deps" | jq -r '.[]' | tr '\n' ' '
        else
            echo ""
        fi
    else
        echo ""
    fi
}