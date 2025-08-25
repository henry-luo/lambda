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
    
    # Capture compilation output and status
    local compile_output
    compile_output=$($compiler $includes $dep_flags -c "$source" -o "$obj_file" $warnings $flags 2>&1)
    local compile_status=$?
    
    # If compilation failed, clean up any partial .d files to prevent stale dependencies
    if [ $compile_status -ne 0 ]; then
        local dep_file="${obj_file%.o}.d"
        rm -f "$dep_file" 2>/dev/null || true
        rm -f "$obj_file" 2>/dev/null || true
    fi
    
    # Output compilation messages
    if [ -n "$compile_output" ]; then
        echo "$compile_output"
    fi
    
    return $compile_status
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
    
    echo "DEBUG: needs_linking called with output_file=$output_file" >&2
    echo "DEBUG: needs_linking received ${#object_files[@]} object files" >&2
    echo "DEBUG: force_rebuild=$force_rebuild" >&2
    
    # If force rebuild is enabled, always link
    if [ "$force_rebuild" = "true" ]; then
        echo "DEBUG: Force rebuild enabled, linking needed" >&2
        return 0
    fi
    
    # If output file doesn't exist, link
    if [ ! -f "$output_file" ]; then
        echo "DEBUG: Output file doesn't exist, linking needed" >&2
        return 0
    fi
    echo "DEBUG: Output file exists: $output_file" >&2
    
    # Get output file timestamp once
    local output_time=$(stat -f "%m" "$output_file" 2>/dev/null || stat -c "%Y" "$output_file" 2>/dev/null)
    if [ -z "$output_time" ]; then
        echo "DEBUG: Can't get timestamp for $output_file, linking needed" >&2
        return 0  # Can't get timestamp, safer to link
    fi
    echo "DEBUG: Output file $output_file timestamp: $output_time" >&2
    
    # Check object files
    for obj_file in "${object_files[@]}"; do
        if [ -f "$obj_file" ]; then
            local obj_time=$(stat -f "%m" "$obj_file" 2>/dev/null || stat -c "%Y" "$obj_file" 2>/dev/null)
            echo "DEBUG: Object file $obj_file timestamp: $obj_time" >&2
            if [ -n "$obj_time" ] && [ "$obj_time" -gt "$output_time" ]; then
                echo "Object file newer than executable: $obj_file (obj: $obj_time > exe: $output_time)" >&2
                return 0
            fi
        else
            echo "Missing object file: $obj_file" >&2
            return 0
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

# Unified function to compile multiple sources to object files
# This is the core compilation function used by both main build and test builds
build_compile_sources() {
    local build_dir="$1"           # Output directory for objects
    local includes="$2"            # Include flags (-I...)
    local warnings="$3"            # Warning flags (-Werror=...)
    local flags="$4"               # Compiler flags (-g, -O0...)
    local enable_deps="$5"         # true/false for .d file generation
    local max_parallel="$6"        # Maximum parallel jobs
    shift 6
    local sources=("$@")           # Array of source files
    
    # Validate inputs
    if [ -z "$build_dir" ]; then
        echo "Error: build_dir is required" >&2
        return 1
    fi
    
    if [ ${#sources[@]} -eq 0 ]; then
        echo "Error: No source files provided" >&2
        return 1
    fi
    
    # Create build directory
    mkdir -p "$build_dir"
    
    # Prepare compilation data
    local files_to_compile=()
    local files_to_compile_obj=()
    local files_to_compile_compiler=()
    local files_to_compile_warnings=()
    local files_to_compile_flags=()
    local object_files=()
    local files_compiled=0
    local files_skipped=0
    
    # Scan sources and determine what needs compilation
    for source in "${sources[@]}"; do
        if [ -f "$source" ]; then
            # Map source to object file
            local obj_name=$(basename "$source" | sed 's/\.[^.]*$//')
            local obj_file="$build_dir/${obj_name}.o"
            object_files+=("$obj_file")
            
            # Check if recompilation is needed
            if needs_recompilation "$source" "$obj_file"; then
                # Auto-detect file type and get appropriate compiler settings
                files_to_compile+=("$source")
                files_to_compile_obj+=("$obj_file")
                files_to_compile_compiler+=("$(get_compiler_for_file "$source")")
                files_to_compile_warnings+=("$(get_warnings_for_file "$source" "$warnings")")
                files_to_compile_flags+=("$(get_flags_for_file "$source" "$flags")")
                files_compiled=$((files_compiled + 1))
            else
                echo "Up-to-date: $source" >&2
                files_skipped=$((files_skipped + 1))
            fi
        else
            echo "Warning: Source file '$source' not found" >&2
            return 1
        fi
    done
    
    # Perform compilation if needed
    local compilation_success=true
    local output=""
    
    if [ ${#files_to_compile[@]} -gt 0 ]; then
        echo "Compiling ${#files_to_compile[@]} source files..." >&2
        
        # Set up parallel jobs
        local parallel_jobs="${max_parallel:-1}"
        if [ -z "$parallel_jobs" ] || [ "$parallel_jobs" -lt 1 ]; then
            parallel_jobs=$(setup_parallel_jobs)
        fi
        
        if [ ${#files_to_compile[@]} -gt 1 ] && [ "$parallel_jobs" -gt 1 ]; then
            echo "Using parallel compilation (max $parallel_jobs jobs)..." >&2
            
            # Use background processes for parallel compilation
            declare -a pids
            local job_count=0
            
            for ((i=0; i<${#files_to_compile[@]}; i++)); do
                local source="${files_to_compile[$i]}"
                local obj_file="${files_to_compile_obj[$i]}"
                local compiler="${files_to_compile_compiler[$i]}"
                local file_warnings="${files_to_compile_warnings[$i]}"
                local file_flags="${files_to_compile_flags[$i]}"
                
                # Start compilation in background
                (
                    local compile_result=$(compile_single_file "$source" "$obj_file" "$compiler" "$includes" "$file_warnings" "$file_flags" "$enable_deps")
                    echo "$compile_result" > "${obj_file}.compile_log"
                    echo $? > "${obj_file}.compile_status"
                ) &
                
                pids+=($!)
                job_count=$((job_count + 1))
                
                # Limit parallel jobs
                if [ $job_count -ge $parallel_jobs ]; then
                    wait ${pids[0]}
                    pids=("${pids[@]:1}")  # Remove first element
                    job_count=$((job_count - 1))
                fi
            done
            
            # Wait for remaining jobs
            for pid in "${pids[@]}"; do
                wait $pid
            done
            
            # Collect results from parallel compilation
            for ((i=0; i<${#files_to_compile[@]}; i++)); do
                local obj_file="${files_to_compile_obj[$i]}"
                
                if [ -f "${obj_file}.compile_status" ]; then
                    local status=$(cat "${obj_file}.compile_status")
                    if [ "$status" -ne 0 ]; then
                        compilation_success=false
                    fi
                fi
                
                if [ -f "${obj_file}.compile_log" ]; then
                    output="$output$(cat "${obj_file}.compile_log")"
                    rm -f "${obj_file}.compile_log"
                fi
                
                rm -f "${obj_file}.compile_status"
            done
        else
            # Sequential compilation
            for ((i=0; i<${#files_to_compile[@]}; i++)); do
                local source="${files_to_compile[$i]}"
                local obj_file="${files_to_compile_obj[$i]}"
                local compiler="${files_to_compile_compiler[$i]}"
                local file_warnings="${files_to_compile_warnings[$i]}"
                local file_flags="${files_to_compile_flags[$i]}"
                
                local compile_result=$(compile_single_file "$source" "$obj_file" "$compiler" "$includes" "$file_warnings" "$file_flags" "$enable_deps")
                local compile_status=$?
                
                output="$output$compile_result"
                
                if [ $compile_status -ne 0 ]; then
                    compilation_success=false
                fi
            done
        fi
    else
        echo "All sources up-to-date." >&2
    fi

    # Output compilation messages to stderr to avoid contaminating object file list
    if [ -n "$output" ]; then
        echo -e "$output" >&2
    fi

    # Generate summary to stderr
    echo "Compilation summary: $files_compiled compiled, $files_skipped up-to-date" >&2

    # Return object files list on success, empty on failure
    if [ "$compilation_success" = "true" ]; then
        # Always return the complete list of object files, even if no compilation occurred
        # This is needed for linking detection to work properly
        printf '%s\n' "${object_files[@]}"
        return 0
    else
        echo "Compilation failed" >&2
        return 1
    fi
}

# Unified function to link object files into executable
# This is the core linking function used by both main build and test builds
build_link_objects() {
    local output_file="$1"         # Output executable path
    local main_objects="$2"        # Primary object files (space-separated)
    local additional_objects="$3"  # Additional objects (space-separated)
    local link_libraries="$4"      # Link libraries (-l...)
    local link_flags="$5"          # Linker flags
    local force_cpp="${6:-auto}"   # true/false/auto for C++ linking
    
    # Validate inputs
    if [ -z "$output_file" ]; then
        echo "Error: output_file is required" >&2
        return 1
    fi
    
    if [ -z "$main_objects" ]; then
        echo "Error: main_objects is required" >&2
        return 1
    fi
    echo "DEBUG: main_objects received: $main_objects" >&2
    
    # Combine all object files
    local all_objects="$main_objects"
    if [ -n "$additional_objects" ]; then
        all_objects="$all_objects $additional_objects"
    fi
    
    # Remove duplicates from object files list
    local unique_objects=""
    for obj in $all_objects; do
        if [[ "$unique_objects" != *"$obj"* ]]; then
            unique_objects="$unique_objects $obj"
        fi
    done
    unique_objects=$(echo "$unique_objects" | sed 's/^ *//')
    
    # Check if linking is needed
    local object_files_array=($unique_objects)
    echo "DEBUG: Checking if linking needed for $output_file" >&2
    echo "DEBUG: Object files: ${object_files_array[*]}" >&2
    echo "DEBUG: Array length: ${#object_files_array[@]}" >&2
    echo "DEBUG: About to call needs_linking..." >&2
    echo "DEBUG: First few object files: ${object_files_array[0]} ${object_files_array[1]} ${object_files_array[2]}" >&2
    
    # Call needs_linking and capture the result
    if needs_linking "$output_file" "${object_files_array[@]}"; then
        echo "DEBUG: needs_linking returned true, proceeding with linking" >&2
    else
        echo "DEBUG: needs_linking returned false, executable up-to-date" >&2
        echo "Executable up-to-date: $output_file"
        return 0
    fi
    
    # Determine linker
    local linker=""
    if [ "$force_cpp" = "true" ] || [[ "$unique_objects" == *".cpp"* ]] || [[ "$link_libraries" == *".cpp"* ]]; then
        linker="${CXX:-clang++}"
    elif [ "$force_cpp" = "false" ]; then
        linker="${CC:-clang}"
    else
        # Auto-detect based on object files and libraries
        if [[ "$unique_objects" == *".cpp"* ]] || [[ "$link_libraries" == *".cpp"* ]]; then
            linker="${CXX:-clang++}"
        else
            linker="${CC:-clang}"
        fi
    fi
    
    # Execute linking
    echo "Linking: $output_file"
    echo "Linker: $linker"
    echo "Objects: $unique_objects"
    if [ -n "$link_libraries" ]; then
        echo "Libraries: $link_libraries"
    fi
    if [ -n "$link_flags" ]; then
        echo "Flags: $link_flags"
    fi
    
    # Execute linking with proper error handling
    local link_result
    link_result=$($linker $unique_objects $link_libraries $link_flags -o "$output_file" 2>&1)
    local link_status=$?
    
    if [ -n "$link_result" ]; then
        echo "$link_result"
    fi
    
    if [ $link_status -eq 0 ]; then
        echo "✅ Successfully linked: $output_file"
        return 0
    else
        echo "❌ Failed to link: $output_file"
        return 1
    fi
}

# Legacy function - kept for compatibility, now uses unified functions
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
    
    # Build command based on mode using unified functions
    case "$mode" in
        compile)
            # Use unified compile function
            local sources_array=("$source")
            if [ -n "$additional_sources" ]; then
                for src in $additional_sources; do
                    sources_array+=("$src")
                done
            fi
            
            local build_dir=$(dirname "$output")
            local objects=$(build_compile_sources "$build_dir" "$includes" "$warnings" "$flags" "$enable_deps" "1" "${sources_array[@]}")
            if [ $? -eq 0 ]; then
                echo "$objects"
                return 0
            else
                return 1
            fi
            ;;
            
        link)
            # Use unified link function
            build_link_objects "$output" "$source" "$additional_sources" "$libs $link_libs" "$linker_flags" "auto"
            return $?
            ;;
            
        *)
            echo "Error: Invalid mode '$mode'. Use 'compile' or 'link'" >&2
            return 1
            ;;
    esac
}
