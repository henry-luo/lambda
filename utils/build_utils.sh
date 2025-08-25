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
    local lib_def=$(jq -r ".libraries[] | select(.name == \"$lib_name\")" "$build_config_file" 2>/dev/null)
    
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
    local lib_path=$(echo "$lib_def" | jq -r '.lib // empty')
    local link_type=$(echo "$lib_def" | jq -r '.link // "dynamic"')
    local nested_libs=$(echo "$lib_def" | jq -r '.libraries[]? // empty' | tr '\n' ' ')
    local lib_special_flags=$(echo "$lib_def" | jq -r '.special_flags // empty')
    
    # Add include path
    if [ -n "$include_path" ]; then
        includes="-I$include_path"
    fi
    
    # ===== NEW: Handle source patterns and explicit source files =====
    
    # Collect sources from all possible sources
    local all_lib_sources=""
    
    # 1. Legacy sources array (for backward compatibility)
    local legacy_sources=$(echo "$lib_def" | jq -r '.sources[]? // empty')
    if [ -n "$legacy_sources" ]; then
        all_lib_sources="$all_lib_sources $legacy_sources"
    fi
    
    # 2. Explicit source_files array
    local explicit_sources=$(echo "$lib_def" | jq -r '.source_files[]? // empty')
    if [ -n "$explicit_sources" ]; then
        while IFS= read -r source; do
            [ -n "$source" ] && all_lib_sources="$all_lib_sources $source"
        done <<< "$explicit_sources"
    fi
    
    # 3. Source patterns that need to be expanded
    local source_patterns=$(echo "$lib_def" | jq -r '.source_patterns[]? // empty')
    if [ -n "$source_patterns" ]; then
        local patterns_array=()
        while IFS= read -r pattern; do
            [ -n "$pattern" ] && patterns_array+=("$pattern")
        done <<< "$source_patterns"
        
        # Expand patterns using our utility function
        if [ ${#patterns_array[@]} -gt 0 ]; then
            local expanded_sources=$(expand_source_patterns "${patterns_array[@]}")
            while IFS= read -r source; do
                [ -n "$source" ] && all_lib_sources="$all_lib_sources $source"
            done <<< "$expanded_sources"
        fi
    fi
    
    # Add collected sources (with deduplication)
    if [ -n "$all_lib_sources" ]; then
        for source in $all_lib_sources; do
            if [[ "$included_sources" != *"$source"* ]]; then
                sources="$sources $source"
                included_sources="$included_sources $source "
            fi
        done
    fi
    
    # ===== Handle legacy object files (for backward compatibility) =====
    local lib_objects_raw=$(echo "$lib_def" | jq -r '.objects[]? // empty')
    
    # Add object files (with deduplication)
    if [ -n "$lib_objects_raw" ]; then
        # Use printf and read in a more reliable way
        while IFS= read -r obj; do
            [ -n "$obj" ] || continue
            # Expand wildcards
            if [ -f "$obj" ]; then
                if [[ "$included_objects" != *"$obj"* ]]; then
                    objects="$objects $obj"
                    included_objects="$included_objects $obj "
                fi
            fi
        done <<< "$lib_objects_raw"
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
                if [ -n "$lib_name_only" ] && [ "$lib_name_only" != "$(basename "$lib_path")" ]; then
                    # Standard library format (extracted a meaningful name)
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
            flags="lib/file.c build/print.o build/strview.o build/transpile.o build/utf.o build/build_ast.o build/lambda-eval.o build/lambda-mem.o build/runner.o build/mir.o build/url.o build/parse.o build/parser.o build/num_stack.o build/input*.o build/format*.o build/strbuf.o build/hashmap.o build/arraylist.o build/variable.o build/buffer.o build/utils.o build/mime-detect.o build/mime-types.o build/datetime.o build/string.o build/utf_string.o lambda/tree-sitter-lambda/libtree-sitter-lambda.a lambda/tree-sitter/libtree-sitter.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a -Ilib/mem-pool/include /opt/homebrew/Cellar/mpdecimal/4.0.1/lib/libmpdec.a /opt/homebrew/lib/libutf8proc.a -lm"
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
            echo "$build_dir/variable.o"
            echo "$build_dir/buffer.o" 
            echo "$build_dir/utils.o"
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
        "url")
            echo "$build_dir/url.o"
            echo "$build_dir/url_parser.o"
            ;;
        "mime-detect")
            echo "$build_dir/mime-detect.o"
            echo "$build_dir/mime-types.o"
            ;;
        "lambda-runtime-full")
            # Return all necessary object files in one line
            echo "$build_dir/lambda-eval.o $build_dir/lambda-mem.o $build_dir/transpile.o $build_dir/transpile-mir.o $build_dir/build_ast.o $build_dir/mir.o $build_dir/runner.o $build_dir/parse.o $build_dir/parser.o $build_dir/print.o $build_dir/input.o $build_dir/input-json.o $build_dir/input-ini.o $build_dir/input-xml.o $build_dir/input-yaml.o $build_dir/input-toml.o $build_dir/input-common.o $build_dir/input-css.o $build_dir/input-csv.o $build_dir/input-eml.o $build_dir/input-html.o $build_dir/input-ics.o $build_dir/input-latex.o $build_dir/input-man.o $build_dir/input-vcf.o $build_dir/input-mark.o $build_dir/input-org.o $build_dir/input-math.o $build_dir/input-rtf.o $build_dir/input-pdf.o $build_dir/input-markup.o $build_dir/format.o $build_dir/format-css.o $build_dir/format-latex.o $build_dir/format-html.o $build_dir/format-ini.o $build_dir/format-json.o $build_dir/format-math.o $build_dir/format-md.o $build_dir/format-org.o $build_dir/format-rst.o $build_dir/format-toml.o $build_dir/format-xml.o $build_dir/format-yaml.o $build_dir/strbuf.o $build_dir/strview.o $build_dir/arraylist.o $build_dir/file.o $build_dir/hashmap.o $build_dir/variable.o $build_dir/buffer.o $build_dir/utils.o $build_dir/url.o $build_dir/utf.o $build_dir/num_stack.o $build_dir/string.o $build_dir/datetime.o $build_dir/mime-detect.o $build_dir/mime-types.o $build_dir/pack.o $build_dir/validate.o $build_dir/validator.o $build_dir/schema_parser.o $build_dir/utf_string.o $build_dir/name_pool.o"
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
    
    local object_files_array=()
    local included_objects=""
    
    # Process each library dependency
    for lib_name in "${library_deps[@]}"; do
        
        local lib_objects=$(get_library_object_files "$lib_name" "$build_dir")
        
        # Add objects, avoiding duplicates (handle newline-separated output)
        if [ -n "$lib_objects" ]; then
            while IFS= read -r obj_file; do
                [ -n "$obj_file" ] || continue
                if [[ "$included_objects" != *"$obj_file"* ]]; then
                    object_files_array+=("$obj_file")
                    included_objects="$included_objects $obj_file "
                fi
            done <<< "$lib_objects"
        fi
    done
    
    # Filter out non-existent objects and return
    local existing_objects_array=()
    for obj_file in "${object_files_array[@]}"; do
        if [ -f "$obj_file" ]; then
            existing_objects_array+=("$obj_file")
        fi
    done
    
    # Join array elements with spaces and return
    local result=""
    for obj in "${existing_objects_array[@]}"; do
        result="$result $obj"
    done
    result=$(echo "$result" | sed 's/^[[:space:]]*//')
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

# Function to map source files to their corresponding object files
# This replaces hardcoded object lists with auto-detection
map_sources_to_objects() {
    local build_dir="$1"
    shift
    local sources=("$@")
    
    local objects=()
    
    for source in "${sources[@]}"; do
        if [ -n "$source" ]; then
            # Extract base filename without extension
            local basename=$(basename "$source")
            local obj_name="${basename%.*}"
            local obj_path="$build_dir/${obj_name}.o"
            
            objects+=("$obj_path")
        fi
    done
    
    # Output object paths (one per line for easy processing)
    printf '%s\n' "${objects[@]}"
}

# Function to expand source patterns into actual source files
# Supports glob patterns like "lambda/input/*.cpp"
expand_source_patterns() {
    local patterns=("$@")
    local expanded_sources=()
    
    for pattern in "${patterns[@]}"; do
        if [ -n "$pattern" ]; then
            # Use shell globbing to expand pattern
            # For zsh/bash compatibility, use a different approach
            local matches=()
            
            # Enable glob expansion
            if [ -n "$ZSH_VERSION" ]; then
                # zsh
                setopt local_options null_glob
                matches=($~pattern)
            else
                # bash
                local old_nullglob=$(shopt -p nullglob 2>/dev/null || echo "shopt -u nullglob")
                shopt -s nullglob
                matches=($pattern)
                eval "$old_nullglob"
            fi
            
            # Add matches to expanded sources
            for match in "${matches[@]}"; do
                if [ -f "$match" ]; then
                    expanded_sources+=("$match")
                fi
            done
        fi
    done
    
    # Output expanded sources (one per line)
    printf '%s\n' "${expanded_sources[@]}"
}

# Function to collect sources from library definition with pattern support
# This replaces the need for hardcoded object lists
collect_library_sources() {
    local lib_name="$1"
    local config_file="$2"
    
    if [ ! -f "$config_file" ] || ! has_jq_support; then
        return 1
    fi
    
    # Get library definition
    local lib_def=$(jq -r ".libraries[] | select(.name == \"$lib_name\")" "$config_file" 2>/dev/null)
    
    if [ -z "$lib_def" ] || [ "$lib_def" = "null" ]; then
        return 1
    fi
    
    local all_sources=()
    
    # Get explicit source files
    local explicit_sources=$(echo "$lib_def" | jq -r '.source_files[]? // empty')
    while IFS= read -r source; do
        [ -n "$source" ] && all_sources+=("$source")
    done <<< "$explicit_sources"
    
    # Get legacy sources array (for backward compatibility)
    local legacy_sources=$(echo "$lib_def" | jq -r '.sources[]? // empty')
    while IFS= read -r source; do
        [ -n "$source" ] && all_sources+=("$source")
    done <<< "$legacy_sources"
    
    # Get and expand source patterns
    local source_patterns=$(echo "$lib_def" | jq -r '.source_patterns[]? // empty')
    if [ -n "$source_patterns" ]; then
        local pattern_sources=()
        while IFS= read -r pattern; do
            if [ -n "$pattern" ]; then
                pattern_sources+=("$pattern")
            fi
        done <<< "$source_patterns"
        
        # Expand patterns and add to sources
        while IFS= read -r source; do
            [ -n "$source" ] && all_sources+=("$source")
        done < <(expand_source_patterns "${pattern_sources[@]}")
    fi
    
    # Remove duplicates and output
    local unique_sources=()
    local seen_sources=""
    
    for source in "${all_sources[@]}"; do
        if [[ "$seen_sources" != *"|$source|"* ]]; then
            unique_sources+=("$source")
            seen_sources="$seen_sources|$source|"
        fi
    done
    
    # Output unique sources (one per line)
    printf '%s\n' "${unique_sources[@]}"
}

# Unified Build Functions for Lambda Project
# These functions bridge between the new library configuration system and build_core.sh functions

# Unified function to compile sources with library dependencies
unified_compile_sources() {
    local sources_str="$1"        # Space-separated list of source files
    local library_names="$2"      # Space-separated list of library names to include
    local config_file="$3"        # Path to build config JSON
    local build_dir="$4"          # Build directory (e.g., "build")
    
    # Default build directory
    if [ -z "$build_dir" ]; then
        build_dir="build"
    fi
    
    # Ensure build_core.sh functions are available
    if ! command -v build_compile_sources >/dev/null 2>&1; then
        source "utils/build_core.sh"
    fi
    
    # Parse sources into array (compatible with both bash and zsh)
    local main_sources=()
    if [ -n "$sources_str" ]; then
        # Use a more compatible method for string splitting
        local temp_ifs="$IFS"
        IFS=' '
        for source in $sources_str; do
            main_sources+=("$source")
        done
        IFS="$temp_ifs"
    fi
    
    # For test builds, compile only the test sources and reuse pre-compiled library objects
    local all_sources=("${main_sources[@]}")
    
    # Prepare includes, warnings, and flags using the same method as compile.sh
    local includes=""
    local warnings=""
    local flags=""
    
    # Process includes - use the working method from compile.sh
    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r include; do
            [ -n "$include" ] && includes="$includes -I$include"
        done < <(jq -r ".includes[]? // empty" "$config_file" 2>/dev/null)
    fi
    
    # Process warnings
    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r warning; do
            [ -n "$warning" ] && warnings="$warnings -Werror=$warning"
        done < <(jq -r ".warnings[]? // empty" "$config_file" 2>/dev/null)
    fi
    
    # Process flags
    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r flag; do
            [ -n "$flag" ] && flags="$flags -$flag"
        done < <(jq -r ".flags[]? // empty" "$config_file" 2>/dev/null)
    fi
    
    # Add library includes and flags if library names are provided
    if [ -n "$library_names" ]; then
        local library_deps=$(resolve_library_dependencies $library_names)
        if [ -n "$library_deps" ]; then
            # Extract includes from library dependencies (starts with -I)
            local lib_includes=$(echo "$library_deps" | grep -o '\-I[^[:space:]]*' | tr '\n' ' ')
            includes="$includes $lib_includes"
            
            # Extract special flags from library dependencies
            local lib_flags=$(echo "$library_deps" | grep -o '\-l[^[:space:]]*\|\-L[^[:space:]]*' | tr '\n' ' ')
            flags="$flags $lib_flags"
        fi
    fi
    
    # Compile test sources only (don't compile library sources, use pre-built objects)
    local test_object_list=$(build_compile_sources "$build_dir" "$includes" "$warnings" "$flags" "false" "1" "${all_sources[@]}")
    local compile_result=$?
    
    if [ $compile_result -eq 0 ]; then
        # Convert newline-separated object list to space-separated for linking compatibility
        local test_objects=$(echo "$test_object_list" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
        
        # If no test objects found in output, try manual mapping
        if [ -z "$test_objects" ]; then
            for source in "${all_sources[@]}"; do
                if [ -f "$source" ]; then
                    local obj_name=$(basename "$source" | sed 's/\.[^.]*$//')
                    local obj_file="$build_dir/${obj_name}.o"
                    if [ -f "$obj_file" ]; then
                        test_objects="$test_objects $obj_file"
                    fi
                fi
            done
            test_objects=$(echo "$test_objects" | sed 's/^ *//' | sed 's/ *$//')
        fi
        
        # For tests, we only return the test objects - library objects are linked separately
        echo "$test_objects" | sed 's/^ *//' | sed 's/ *$//'
        return 0
    else
        echo "$test_object_list" >&2  # Show error messages
        return 1
    fi
}

# Unified function to link objects with library dependencies
unified_link_objects() {
    local output_file="$1"         # Output executable path
    local test_objects="$2"        # Space-separated list of test object files  
    local library_names="$3"       # Space-separated list of library names
    local config_file="$4"         # Path to build config JSON
    
    # Ensure build_core.sh functions are available
    if ! command -v build_link_objects >/dev/null 2>&1; then
        source "utils/build_core.sh"
    fi
    
    # Start with test objects
    local all_objects="$test_objects"
    
    # Resolve library dependencies to get library objects and flags
    local library_flags=""
    local library_objects=""
    
    if [ -n "$library_names" ]; then
        local library_deps=$(resolve_library_dependencies $library_names)
        if [ -n "$library_deps" ]; then
            # Extract static libraries (.a files) - need word boundaries
            local static_libs=$(echo "$library_deps" | grep -oE '[^[:space:]]+\.a' | tr '\n' ' ')
            
            # Extract library paths (-L flags)
            local lib_paths=$(echo "$library_deps" | grep -oE '\-L[^[:space:]]+' | tr '\n' ' ')
            
            # Extract link libraries (-l flags) with proper word boundaries
            local link_libs=$(echo "$library_deps" | grep -o ' -l[a-zA-Z][a-zA-Z0-9_+-]*' | tr '\n' ' ')
            
            # Extract object files (.o files) from pre-built objects
            # For tests, we need to get the corresponding .o files for the .cpp/.c source files
            local source_files=$(echo "$library_deps" | grep -oE '[^[:space:]]+\.(cpp|c)' | tr '\n' ' ')
            
            # Convert source files to object files in build directory
            local lib_obj_files=""
            for source_file in $source_files; do
                if [ -f "$source_file" ]; then
                    local obj_name=$(basename "$source_file" | sed 's/\.[^.]*$//')
                    local obj_file="build/${obj_name}.o"
                    if [ -f "$obj_file" ]; then
                        lib_obj_files="$lib_obj_files $obj_file"
                    fi
                fi
            done
            
            # Combine library flags
            library_flags="$static_libs $lib_paths $link_libs"
            library_objects="$lib_obj_files"
        fi
    fi
    
    # Combine test objects with library objects
    all_objects="$all_objects $library_objects"
    
    # Get linker flags from config
    local linker_flags=""
    while IFS= read -r flag; do
        [ -n "$flag" ] && linker_flags="$linker_flags -$flag"
    done < <(get_json_array "linker_flags" "$config_file" "")
    
    # Use the existing build_link_objects from build_core.sh
    if ! build_link_objects "$output_file" "$all_objects" "" "$library_flags" "$linker_flags" "auto"; then
        echo "❌ Failed to link executable: $output_file" >&2
        return 1
    fi
    
    return 0
}