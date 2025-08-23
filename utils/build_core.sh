#!/bin/bash

# Shared Build Core Functions for Lambda Project
# Common compilation and linking functions used by both compile.sh and test builds

# Color definitions for consistent output
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
BLUE="\033[0;34m"
RESET="\033[0m"

# Function to determine if a file is C++ based on extension
is_cpp_file() {
    local file="$1"
    case "$file" in
        *.cpp|*.cc|*.cxx|*.c++|*.C|*.CPP)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# Function to get appropriate compiler and flags for a file
get_compiler_for_file() {
    local file="$1"
    local base_cc="${CC:-clang}"
    local base_cxx="${CXX:-clang++}"
    
    if is_cpp_file "$file"; then
        echo "$base_cxx"
    else
        echo "$base_cc"
    fi
}

# Function to get appropriate warnings for a file type
get_warnings_for_file() {
    local file="$1"
    local warnings="$2"
    
    if is_cpp_file "$file"; then
        # Remove C-specific warnings that don't apply to C++
        echo "$warnings" | sed 's/-Werror=incompatible-pointer-types//g' | sed 's/-Wincompatible-pointer-types//g'
    else
        echo "$warnings"
    fi
}

# Function to get appropriate flags for a file type
get_flags_for_file() {
    local file="$1"
    local base_flags="$2"
    
    if is_cpp_file "$file"; then
        local cpp_flags="$base_flags -std=c++17"
        if [ "$CROSS_COMPILE" = "true" ]; then
            cpp_flags="$cpp_flags -fpermissive -Wno-fpermissive"
        fi
        echo "$cpp_flags"
    else
        echo "$base_flags -std=c99"
    fi
}

# Function to compile a single file (for parallel execution)
compile_single_file() {
    local source="$1"
    local obj_file="$2"
    local compiler="$3"
    local includes="$4"
    local warnings="$5"
    local flags="$6"
    local enable_deps="${7:-true}"
    
    echo "Compiling: $source -> $obj_file"
    
    # Add -MMD -MP flags for automatic dependency generation if enabled
    local dep_flags=""
    if [ "$enable_deps" = "true" ]; then
        dep_flags="-MMD -MP"
    fi
    
    $compiler $includes $dep_flags -c "$source" -o "$obj_file" $warnings $flags 2>&1
}

# Function to check if source file needs recompilation
needs_recompilation() {
    local source_file="$1"
    local object_file="$2"
    local force_rebuild="${3:-false}"
    local dep_file="${object_file%.o}.d"
    
    # If force rebuild is enabled, always recompile
    if [ "$force_rebuild" = "true" ]; then
        return 0
    fi
    
    # If object file doesn't exist, recompile
    if [ ! -f "$object_file" ]; then
        return 0
    fi
    
    # If source file is newer than object file, recompile
    if [ "$source_file" -nt "$object_file" ]; then
        return 0
    fi
    
    # Check precise dependencies from .d file if it exists
    if [ -f "$dep_file" ]; then
        # Check all dependencies at once
        local newer_deps=$(awk '
            { 
                # Remove line continuations and target
                gsub(/\\$/, "")
                gsub(/^[^:]*:/, "")
                # Split into individual files
                for(i=1; i<=NF; i++) {
                    if($i != "" && $i != "'$object_file'") {
                        print $i
                    }
                }
            }' "$dep_file" | while read -r dep; do
                [ -n "$dep" ] && [ -f "$dep" ] && [ "$dep" -nt "$object_file" ] && echo "$dep" && break
            done)
        
        if [ -n "$newer_deps" ]; then
            echo "Dependency changed: $newer_deps"
            return 0
        fi
        
        # All dependencies checked, no rebuild needed
        return 1
    fi
    
    # No .d file exists, assume recompilation needed for safety
    return 0
}

# Function to check if linking is needed
needs_linking() {
    local output_file="$1"
    shift
    local object_files=("$@")
    local force_rebuild="${FORCE_REBUILD:-false}"
    
    # If force rebuild is enabled, always link
    if [ "$force_rebuild" = "true" ]; then
        return 0
    fi
    
    # If output file doesn't exist, link
    if [ ! -f "$output_file" ]; then
        return 0
    fi
    
    # Get output file timestamp once
    local output_time=$(stat -f "%m" "$output_file" 2>/dev/null || stat -c "%Y" "$output_file" 2>/dev/null)
    if [ -z "$output_time" ]; then
        return 0  # Can't get timestamp, safer to link
    fi
    
    # Check object files
    for obj_file in "${object_files[@]}"; do
        if [ -f "$obj_file" ]; then
            local obj_time=$(stat -f "%m" "$obj_file" 2>/dev/null || stat -c "%Y" "$obj_file" 2>/dev/null)
            if [ -n "$obj_time" ] && [ "$obj_time" -gt "$output_time" ]; then
                return 0
            fi
        fi
    done
    
    # No linking needed
    return 1
}

# Enhanced function to format diagnostics with clickable links
format_diagnostics() {
    local diagnostic_type="$1"
    local grep_pattern="$2"
    local color="$3"
    local output="$4"
    
    local diagnostics=$(echo "$output" | grep "$grep_pattern" | head -20)  # Limit to 20 entries
    if [ -n "$diagnostics" ]; then
        echo
        echo -e "${color}${diagnostic_type} Summary (clickable links):${RESET}"
        echo -e "${color}=====================================${RESET}"
        
        echo "$diagnostics" | while IFS= read -r line; do
            # Extract file path and line number from compiler output
            # Supports formats like: file.c:123:45: error: message
            if echo "$line" | grep -qE '^[^:]+:[0-9]+:[0-9]*:'; then
                # Extract components using more robust pattern
                local file_path=$(echo "$line" | sed -E 's/^([^:]+):[0-9]+:[0-9]*:.*/\1/')
                local line_num=$(echo "$line" | sed -E 's/^[^:]+:([0-9]+):[0-9]*:.*/\1/')
                local col_num=$(echo "$line" | sed -E 's/^[^:]+:[0-9]+:([0-9]*):.*$/\1/')
                local message=$(echo "$line" | sed -E 's/^[^:]+:[0-9]+:[0-9]*:[[:space:]]*(.*)/\1/')
                
                # Handle case where column number might be missing
                if [ -z "$col_num" ]; then
                    col_num="1"
                fi
                
                # Convert absolute path to relative path if needed
                if [[ "$file_path" == /* ]]; then
                    # Convert absolute path to relative path
                    local current_dir="$(pwd)"
                    if [[ "$file_path" == "$current_dir"/* ]]; then
                        file_path="${file_path#$current_dir/}"
                    fi
                fi
                
                # Output clickable link (VS Code and most modern terminals support this format)
                echo -e "  ${color}${file_path}:${line_num}:${col_num}${RESET} - $message"
            else
                # Fallback for non-standard format
                echo -e "  ${color}$line${RESET}"
            fi
        done
        
        local total_count=$(echo "$output" | grep -c "$grep_pattern")
        if [ "$total_count" -gt 20 ]; then
            echo -e "  ${color}... and $(($total_count - 20)) more${RESET}"
        fi
    fi
}

# Function to generate build summary
generate_build_summary() {
    local output="$1"
    local files_compiled="$2"
    local files_skipped="$3"
    local linking_performed="$4"
    local build_dir="$5"
    local target_type="${6:-main}"  # main, test, etc.
    
    # Count errors and warnings (ignores color codes)
    local num_errors=$(echo "$output" | grep -c "error:")
    local num_warnings=$(echo "$output" | grep -c "warning:")
    local num_notes=$(echo "$output" | grep -c "note:")
    
    # Format errors with clickable links
    if [ "$num_errors" -gt 0 ]; then
        format_diagnostics "ERRORS" "error:" "$RED" "$output"
    fi
    
    echo
    echo -e "${YELLOW}Build Summary ($target_type):${RESET}"
    if [ "$num_errors" -gt 0 ]; then
        echo -e "${RED}Errors:   $num_errors${RESET}"
    else
        echo -e "Errors:   $num_errors"
    fi
    echo -e "${YELLOW}Warnings: $num_warnings${RESET}"
    echo "Files compiled: $files_compiled"
    echo "Files up-to-date: $files_skipped"
    if [ "$linking_performed" = "true" ]; then
        echo "Linking: performed"
    else
        echo "Linking: skipped (up-to-date)"
    fi
    
    # Show dependency tracking info
    if [ -n "$build_dir" ] && [ -d "$build_dir" ]; then
        local dep_files_count=$(find "$build_dir" -name "*.d" 2>/dev/null | wc -l | tr -d ' ')
        if [ "$dep_files_count" -gt 0 ]; then
            echo "Dependency tracking: $dep_files_count .d files (precise tracking)"
        else
            echo "Dependency tracking: header cache fallback"
        fi
    fi
    
    # Return success/failure status
    if [ "$num_errors" -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

# Function to perform parallel compilation
compile_files_parallel() {
    local max_jobs="$1"
    shift
    local file_data=("$@")  # Array of "source|object|compiler|includes|warnings|flags"
    
    if [ ${#file_data[@]} -eq 0 ]; then
        return 0
    fi
    
    local output=""
    local compilation_success=true
    
    if [ ${#file_data[@]} -gt 1 ] && [ "$max_jobs" -gt 1 ]; then
        echo "Compiling ${#file_data[@]} files in parallel (max $max_jobs jobs)..."
        
        # Use background processes for parallel compilation
        declare -a pids
        local job_count=0
        
        for file_info in "${file_data[@]}"; do
            IFS='|' read -r source obj_file compiler includes warnings flags <<< "$file_info"
            
            # Start compilation in background
            (
                compile_result=$(compile_single_file "$source" "$obj_file" "$compiler" "$includes" "$warnings" "$flags")
                echo "$compile_result" > "${obj_file}.compile_log"
                echo $? > "${obj_file}.compile_status"
            ) &
            
            pids+=($!)
            job_count=$((job_count + 1))
            
            # Limit parallel jobs
            if [ $job_count -ge $max_jobs ]; then
                wait ${pids[0]}
                pids=("${pids[@]:1}")  # Remove first element
                job_count=$((job_count - 1))
            fi
        done
        
        # Wait for remaining jobs
        for pid in "${pids[@]}"; do
            wait $pid
        done
        
        # Collect results
        for file_info in "${file_data[@]}"; do
            IFS='|' read -r source obj_file compiler includes warnings flags <<< "$file_info"
            
            if [ -f "${obj_file}.compile_log" ]; then
                compile_output=$(cat "${obj_file}.compile_log")
                if [ -n "$compile_output" ]; then
                    output="$output\n$compile_output"
                fi
                rm -f "${obj_file}.compile_log"
            fi
            
            if [ -f "${obj_file}.compile_status" ]; then
                status=$(cat "${obj_file}.compile_status")
                if [ "$status" -ne 0 ]; then
                    compilation_success=false
                fi
                rm -f "${obj_file}.compile_status"
            fi
        done
    else
        # Sequential compilation
        for file_info in "${file_data[@]}"; do
            IFS='|' read -r source obj_file compiler includes warnings flags <<< "$file_info"
            
            compile_output=$(compile_single_file "$source" "$obj_file" "$compiler" "$includes" "$warnings" "$flags")
            if [ $? -ne 0 ]; then
                compilation_success=false
            fi
            if [ -n "$compile_output" ]; then
                output="$output\n$compile_output"
            fi
        done
    fi
    
    # Output captured messages
    echo -e "$output"
    
    if [ "$compilation_success" = "true" ]; then
        return 0
    else
        return 1
    fi
}

# Function to set up parallel compilation jobs
setup_parallel_jobs() {
    local requested_jobs="$1"
    
    if [ -n "$requested_jobs" ]; then
        echo "$requested_jobs"
        return
    fi
    
    # Auto-detect number of CPU cores
    local parallel_jobs
    if command -v nproc >/dev/null 2>&1; then
        parallel_jobs=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        parallel_jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo "1")
    else
        parallel_jobs=1
    fi
    
    # Limit to reasonable maximum
    if [ "$parallel_jobs" -gt 8 ]; then
        parallel_jobs=8
    fi
    
    echo "$parallel_jobs"
}

# Common function to compile source file to object file
# This is the EXACT same logic used by both main build and test builds
build_compile_to_object() {
    local source="$1"
    local object="$2"
    local includes="$3"
    local warnings="$4"
    local flags="$5"
    local enable_deps="${6:-true}"
    
    # Validate inputs
    if [ -z "$source" ] || [ -z "$object" ]; then
        echo "Error: source and object parameters required" >&2
        return 1
    fi
    
    # Auto-detect appropriate compiler for this file
    local compiler=$(get_compiler_for_file "$source")
    
    # Adjust warnings and flags for file type (same as main build)
    local final_warnings=$(get_warnings_for_file "$source" "$warnings")
    local final_flags=$(get_flags_for_file "$source" "$flags")
    
    # Build dependency flags
    local dep_flags=""
    if [ "$enable_deps" = "true" ]; then
        dep_flags="-MMD -MP"
    fi
    
    # Execute compilation (exact same command as main build)
    echo "Compiling: $source -> $object"
    $compiler $includes $dep_flags -c "$source" -o "$object" $final_warnings $final_flags 2>&1
}

# Common function to link object files into executable
# This is the EXACT same logic used by both main build and test builds
build_link_executable() {
    local output="$1"
    local object_files="$2"
    local libs="$3"
    local link_libs="$4"
    local linker_flags="$5"
    local force_cxx="${6:-auto}"
    
    # Validate inputs
    if [ -z "$output" ] || [ -z "$object_files" ]; then
        echo "Error: output and object_files parameters required" >&2
        return 1
    fi
    
    # Determine linker (same logic as main build)
    local linker=""
    if [ "$force_cxx" = "true" ] || [[ "$object_files" == *".cpp"* ]] || [[ "$libs" == *".cpp"* ]]; then
        # Use C++ linker if we have C++ objects or forced
        linker="${CXX:-clang++}"
    else
        # Use C linker for pure C projects
        linker="${CC:-clang}"
    fi
    
    # Execute linking (exact same command as main build)
    echo "Linking: $output"
    $linker $object_files $libs $link_libs $linker_flags -o "$output" 2>&1
}

# Legacy function - kept for compatibility, now uses common functions
build_compile_cmd() {
    local mode=""
    local source=""
    local output=""
    local compiler=""
    local includes=""
    local warnings=""
    local flags=""
    local libs=""
    local link_libs=""
    local linker_flags=""
    local enable_deps="true"
    local additional_sources=""
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --mode=*)
                mode="${1#*=}"
                shift
                ;;
            --source=*)
                source="${1#*=}"
                shift
                ;;
            --output=*)
                output="${1#*=}"
                shift
                ;;
            --compiler=*)
                compiler="${1#*=}"
                shift
                ;;
            --includes=*)
                includes="${1#*=}"
                shift
                ;;
            --warnings=*)
                warnings="${1#*=}"
                shift
                ;;
            --flags=*)
                flags="${1#*=}"
                shift
                ;;
            --libs=*)
                libs="${1#*=}"
                shift
                ;;
            --link-libs=*)
                link_libs="${1#*=}"
                shift
                ;;
            --linker-flags=*)
                linker_flags="${1#*=}"
                shift
                ;;
            --additional-sources=*)
                additional_sources="${1#*=}"
                shift
                ;;
            --dependencies)
                enable_deps="true"
                shift
                ;;
            --no-dependencies)
                enable_deps="false"
                shift
                ;;
            *)
                echo "Unknown option: $1" >&2
                return 1
                ;;
        esac
    done
    
    # Validate required parameters
    if [ -z "$mode" ] || [ -z "$source" ] || [ -z "$output" ]; then
        echo "Error: --mode, --source, and --output are required" >&2
        return 1
    fi
    
    # Build command based on mode using common functions
    case "$mode" in
        compile)
            # Use common compile function
            build_compile_to_object "$source" "$output" "$includes" "$warnings" "$flags" "$enable_deps"
            return $?
            ;;
            
        link)
            # For legacy compatibility - compile all sources and link directly
            local all_sources="$source"
            if [ -n "$additional_sources" ]; then
                all_sources="$all_sources $additional_sources"
            fi
            
            # This is a simplified version - real usage should use the two-step process
            local compiler=$(get_compiler_for_file "$source")
            local final_warnings=$(get_warnings_for_file "$source" "$warnings")
            local final_flags=$(get_flags_for_file "$source" "$flags")
            
            echo "$compiler $final_flags $final_warnings $includes $all_sources -o \"$output\" $libs $link_libs $linker_flags"
            return 0
            ;;
            
        *)
            echo "Error: Invalid mode '$mode'. Use 'compile' or 'link'" >&2
            return 1
            ;;
    esac
}
