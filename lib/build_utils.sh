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

# Function to get the global compiler setting with C/C++ variant support
get_global_compiler() {
    local variant="$1"  # Optional: "c" or "cpp" to get C or C++ variant
    local config_file="${BUILD_CONFIG_FILE:-build_lambda_config.json}"
    
    local base_compiler="clang"  # Default fallback
    
    if [ -f "$config_file" ] && has_jq_support; then
        local config_compiler=$(jq -r '.compiler // empty' "$config_file" 2>/dev/null)
        if [ -n "$config_compiler" ] && [ "$config_compiler" != "null" ]; then
            base_compiler="$config_compiler"
        fi
    fi
    
    # Return appropriate variant
    case "$variant" in
        "cpp"|"c++")
            if [ "$base_compiler" = "clang" ]; then
                echo "clang++"
            elif [ "$base_compiler" = "gcc" ]; then
                echo "g++"
            else
                echo "$base_compiler++"
            fi
            ;;
        "c"|*)
            echo "$base_compiler"
            ;;
    esac
}

# Function to check if jq is available and working
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
    
    local resolved_flags=""
    local all_includes=""
    local all_sources=""
    local all_objects=""
    local all_static_libs=""
    local all_dynamic_libs=""
    local all_special_flags=""
    
    # Use associative arrays to track what we've already included (requires bash 4+)
    # For bash 3.2 compatibility, we'll use simple string tracking
    local included_objects=""
    local included_sources=""
    
    # Process each library dependency
    for lib_name in "${library_deps_array[@]}"; do
        local lib_result=$(resolve_single_library_dependency "$lib_name" "$build_config_file" "$included_objects" "$included_sources")
        if [ -n "$lib_result" ]; then
            # Parse the result format: "includes|sources|objects|static_libs|dynamic_libs|special_flags|updated_included_objects|updated_included_sources"
            local includes=$(echo "$lib_result" | cut -d'|' -f1)
            local sources=$(echo "$lib_result" | cut -d'|' -f2)
            local objects=$(echo "$lib_result" | cut -d'|' -f3)
            local static_libs=$(echo "$lib_result" | cut -d'|' -f4)
            local dynamic_libs=$(echo "$lib_result" | cut -d'|' -f5)
            local special_flags=$(echo "$lib_result" | cut -d'|' -f6)
            included_objects=$(echo "$lib_result" | cut -d'|' -f7)
            included_sources=$(echo "$lib_result" | cut -d'|' -f8)
            
            # Accumulate results
            all_includes="$all_includes $includes"
            all_sources="$all_sources $sources"
            all_objects="$all_objects $objects"
            all_static_libs="$all_static_libs $static_libs"
            all_dynamic_libs="$all_dynamic_libs $dynamic_libs"
            all_special_flags="$all_special_flags $special_flags"
        fi
    done
    
    # Build final resolved flags string
    resolved_flags="$all_includes $all_sources $all_objects $all_static_libs $all_dynamic_libs $all_special_flags"
    echo "$resolved_flags"
}

# Helper function to resolve a single library dependency with deduplication
resolve_single_library_dependency() {
    local lib_name="$1"
    local build_config_file="$2"
    local included_objects="$3"
    local included_sources="$4"
    
    if [ ! -f "$build_config_file" ] || ! has_jq_support; then
        # Fallback to legacy hardcoded resolution for backwards compatibility
        local legacy_flags=$(resolve_library_legacy "$lib_name")
        echo "$legacy_flags||||||$included_objects|$included_sources"
        return
    fi
    
    # Get library definition from config
    local lib_def=$(jq -r ".libraries[] | select(.name == "$lib_name")" "$build_config_file" 2>/dev/null)
    
    if [ -z "$lib_def" ] || [ "$lib_def" = "null" ]; then
        # Library not found in config, try legacy resolution
        local legacy_flags=$(resolve_library_legacy "$lib_name")
        echo "$legacy_flags||||||$included_objects|$included_sources"
        return
    fi
    
    local includes=""
    local sources=""
    local objects=""
    local static_libs=""
    local dynamic_libs=""
    local special_flags=""
    
    # Parse library definition and build flags
    local include_path=$(echo "$lib_def" | jq -r '.include // empty')
    local lib_sources=$(echo "$lib_def" | jq -r '.sources[]? // empty' | tr '
' ' ')
    local lib_objects=$(echo "$lib_def" | jq -r '.objects[]? // empty' | tr '
' ' ')
    local lib_path=$(echo "$lib_def" | jq -r '.lib // empty')
    local link_type=$(echo "$lib_def" | jq -r '.link // "dynamic"')
    local nested_libs=$(echo "$lib_def" | jq -r '.libraries[]? // empty' | tr '
' ' ')
    local lib_special_flags=$(echo "$lib_def" | jq -r '.special_flags // empty')
    
    # Add include path
    if [ -n "$include_path" ]; then
        includes="-I$include_path"
    fi
    
    # Add source files (with deduplication)
    if [ -n "$lib_sources" ]; then
        for source in $lib_sources; do
            if [[ "$included_sources" != *"$source"* ]]; then
                sources="$sources $source"
                included_sources="$included_sources $source "
            fi
        done
    fi
    
    # Add object files (with deduplication)
    if [ -n "$lib_objects" ]; then
        for obj in $lib_objects; do
            # Expand wildcards
            local expanded_objects=$(ls $obj 2>/dev/null || echo "")
            for expanded_obj in $expanded_objects; do
                if [[ "$included_objects" != *"$expanded_obj"* ]]; then
                    objects="$objects $expanded_obj"
                    included_objects="$included_objects $expanded_obj "
                fi
            done
        done
    fi
    
    # Add special flags
    if [ -n "$lib_special_flags" ]; then
        special_flags="$lib_special_flags"
    fi
    
    # Handle library linking based on type
    case "$link_type" in
        "static")
            if [ -n "$lib_path" ]; then
                static_libs="$lib_path"
            fi
            ;;
        "dynamic")
            if [ -n "$lib_path" ]; then
                local lib_dir=$(dirname "$lib_path")
                local lib_name_only=$(basename "$lib_path" | sed 's/^lib//' | sed 's/\.[^.]*$//')
                if [ "$lib_name_only" != "$lib_path" ]; then
                    # Standard library format
                    dynamic_libs="-L$lib_dir -l$lib_name_only"
                else
                    # Just a directory path - extract library name from parent library
                    case "$lib_name" in
                        "criterion")
                            dynamic_libs="-L$lib_path -lcriterion"
                            ;;
                        *)
                            dynamic_libs="-L$lib_path"
                            ;;
                    esac
                fi
            fi
            ;;
        "inline"|*)
            # Source files are already handled above
            ;;
    esac
    
    # Recursively resolve nested library dependencies
    if [ -n "$nested_libs" ]; then
        for nested_lib in $nested_libs; do
            local nested_result=$(resolve_single_library_dependency "$nested_lib" "$build_config_file" "$included_objects" "$included_sources")
            if [ -n "$nested_result" ]; then
                # Parse nested result and accumulate
                local nested_includes=$(echo "$nested_result" | cut -d'|' -f1)
                local nested_sources=$(echo "$nested_result" | cut -d'|' -f2)
                local nested_objects=$(echo "$nested_result" | cut -d'|' -f3)
                local nested_static_libs=$(echo "$nested_result" | cut -d'|' -f4)
                local nested_dynamic_libs=$(echo "$nested_result" | cut -d'|' -f5)
                local nested_special_flags=$(echo "$nested_result" | cut -d'|' -f6)
                included_objects=$(echo "$nested_result" | cut -d'|' -f7)
                included_sources=$(echo "$nested_result" | cut -d'|' -f8)
                
                includes="$includes $nested_includes"
                sources="$sources $nested_sources"
                objects="$objects $nested_objects"
                static_libs="$static_libs $nested_static_libs"
                dynamic_libs="$dynamic_libs $nested_dynamic_libs"
                special_flags="$special_flags $nested_special_flags"
            fi
        done
    fi
    
    # Return results in pipe-separated format
    echo "$includes|$sources|$objects|$static_libs|$dynamic_libs|$special_flags|$included_objects|$included_sources"
}



# Legacy library resolution for backwards compatibility
resolve_library_legacy() {
    local lib_name="$1"
    local flags=""
    
    case "$lib_name" in
        "strbuf")
            flags="lib/strbuf.c"
            ;;
        "strview")
            flags="lib/strview.c"
            ;;
        "mem-pool")
            flags="lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c -Ilib/mem-pool/include"
            ;;
        "num_stack")
            flags="lib/num_stack.c"
            ;;
        "datetime")
            flags="lib/datetime.c"
            ;;
        "string")
            flags="lib/string.c"
            ;;
        "mime-detect")
            flags="lambda/input/mime-detect.c lambda/input/mime-types.c"
            ;;
        "criterion")
            flags="-I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -lcriterion"
            ;;
        "lambda-runtime-full")
            # Complex legacy fallback - should be defined in config instead
            flags="lib/file.c build/print.o build/strview.o build/transpile.o build/utf.o build/build_ast.o build/lambda-eval.o build/lambda-mem.o build/runner.o build/mir.o build/url.o build/parse.o build/parser.o build/num_stack.o build/input*.o build/format*.o build/strbuf.o build/hashmap.o build/arraylist.o build/variable.o build/buffer.o build/utils.o build/mime-detect.o build/mime-types.o build/datetime.o build/string.o build/unicode_string.o lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a -Ilib/mem-pool/include /opt/homebrew/Cellar/mpdecimal/4.0.1/lib/libmpdec.a -lm -L/Users/henryluo/Projects/Jubily/icu-compact/lib -licui18n -licuuc -licudata"
            ;;
    esac
    
    echo "$flags"
}
# Phase 6: Build System Integration Functions

# Function to validate that main build objects are up-to-date
validate_build_objects() {
    local build_dir="${1:-build}"
    local config_file="${BUILD_CONFIG_FILE:-build_lambda_config.json}"
    
    # Check if build directory exists
    if [ ! -d "$build_dir" ]; then
        echo "Build directory '$build_dir' not found" >&2
        return 1
    fi
    
    # Check if main executable exists
    if [ ! -f "./lambda.exe" ]; then
        echo "Main executable 'lambda.exe' not found" >&2
        return 1
    fi
    
    # Basic validation - check if we have essential object files
    local essential_objects=(
        "lambda-eval.o"
        "lambda-mem.o"
        "strbuf.o"
        "hashmap.o"
        "variable.o"
    )
    
    for obj in "${essential_objects[@]}"; do
        if [ ! -f "$build_dir/$obj" ]; then
            echo "Essential object file '$build_dir/$obj' not found" >&2
            return 1
        fi
    done
    
    return 0
}

# Function to get required object files for a specific library
get_library_object_files() {
    local lib_name="$1"
    local build_dir="${2:-build}"
    local config_file="${BUILD_CONFIG_FILE:-build_lambda_config.json}"
    
    # Map library names to their required object files
    case "$lib_name" in
        "strbuf")
            echo "$build_dir/strbuf.o"
            ;;
        "strview")
            echo "$build_dir/strview.o"
            ;;
        "mem-pool")
            echo "$build_dir/variable.o $build_dir/buffer.o $build_dir/utils.o"
            ;;
        "num_stack")
            echo "$build_dir/num_stack.o"
            ;;
        "datetime")
            echo "$build_dir/datetime.o"
            ;;
        "string")
            echo "$build_dir/string.o"
            ;;
        "mime-detect")
            echo "$build_dir/mime-detect.o $build_dir/mime-types.o"
            ;;
        "lambda-runtime-full")
            # Return all necessary object files in one line
            echo "$build_dir/lambda-eval.o $build_dir/lambda-mem.o $build_dir/transpile.o $build_dir/transpile-mir.o $build_dir/build_ast.o $build_dir/mir.o $build_dir/runner.o $build_dir/parse.o $build_dir/parser.o $build_dir/print.o $build_dir/input.o $build_dir/input-json.o $build_dir/input-ini.o $build_dir/input-xml.o $build_dir/input-yaml.o $build_dir/input-toml.o $build_dir/input-common.o $build_dir/input-css.o $build_dir/input-csv.o $build_dir/input-eml.o $build_dir/input-html.o $build_dir/input-ics.o $build_dir/input-latex.o $build_dir/input-man.o $build_dir/input-vcf.o $build_dir/input-mark.o $build_dir/input-org.o $build_dir/input-math.o $build_dir/input-rtf.o $build_dir/input-pdf.o $build_dir/input-markup.o $build_dir/format.o $build_dir/format-css.o $build_dir/format-latex.o $build_dir/format-html.o $build_dir/format-ini.o $build_dir/format-json.o $build_dir/format-math.o $build_dir/format-md.o $build_dir/format-org.o $build_dir/format-rst.o $build_dir/format-toml.o $build_dir/format-xml.o $build_dir/format-yaml.o $build_dir/strbuf.o $build_dir/strview.o $build_dir/arraylist.o $build_dir/file.o $build_dir/hashmap.o $build_dir/variable.o $build_dir/buffer.o $build_dir/utils.o $build_dir/url.o $build_dir/utf.o $build_dir/num_stack.o $build_dir/string.o $build_dir/datetime.o $build_dir/mime-detect.o $build_dir/mime-types.o $build_dir/pack.o $build_dir/validate.o $build_dir/validator.o $build_dir/schema_parser.o $build_dir/unicode_string.o"
            ;;
        *)
            # Unknown library - return empty
            echo ""
            ;;
    esac
}

# Function to get minimal object file set for a test
get_minimal_object_set() {
    local test_source="$1"
    shift
    local library_deps=("$@")
    local build_dir="${BUILD_DIR:-build}"
    
    local object_files=""
    local included_objects=""
    
    # Process each library dependency
    for lib_name in "${library_deps[@]}"; do
        # Skip criterion as it's handled separately
        if [ "$lib_name" = "criterion" ]; then
            continue
        fi
        
        local lib_objects=$(get_library_object_files "$lib_name" "$build_dir")
        
        # Add objects, avoiding duplicates
        for obj_file in $lib_objects; do
            if [[ "$included_objects" != *"$obj_file"* ]]; then
                object_files="$object_files $obj_file"
                included_objects="$included_objects $obj_file "
            fi
        done
    done
    
    # Filter out non-existent objects and return
    local existing_objects=""
    for obj_file in $object_files; do
        if [ -f "$obj_file" ]; then
            existing_objects="$existing_objects $obj_file"
        fi
    done
    
    # Trim leading/trailing spaces and return
    local result=$(echo "$existing_objects" | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
    echo "$result"
}

# Function to check build prerequisites for testing
check_build_prerequisites() {
    local build_dir="${1:-build}"
    
    # Check if main build is completed
    if [ ! -f "./lambda.exe" ]; then
        echo "Main build not found. Run 'make build' first." >&2
        return 1
    fi
    
    # Validate build objects
    if ! validate_build_objects "$build_dir"; then
        echo "Build objects validation failed. Run 'make build' to ensure objects are current." >&2
        return 1
    fi
    
    return 0
}