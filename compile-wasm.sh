#!/bin/bash

# WASM Compilation Script for Lambda Project
# Compiles lambda/input, lambda/format, and lambda/validator C files into WASM using WASI SDK
# Usage: ./compile-wasm.sh [--debug] [--force] [--jobs=N] [--clean-deps] [--help]

set -e  # Exit on any error

# Default configuration
CONFIG_FILE="build_lambda_wasm_config.json"
WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
FORCE_REBUILD=false
PARALLEL_JOBS=""
DEBUG_BUILD=false

# Colors for output
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
BLUE="\033[0;34m"
RESET="\033[0m"

# Function to clean dependency files (.d files)
clean_dependency_files() {
    local build_dir="${1:-build_wasm}"
    if [ -d "$build_dir" ]; then
        echo "Cleaning dependency files in $build_dir..."
        find "$build_dir" -name "*.d" -delete 2>/dev/null || true
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug|-d)
            DEBUG_BUILD=true
            shift
            ;;
        --force|-f)
            FORCE_REBUILD=true
            shift
            ;;
        --clean-deps)
            echo "Cleaning dependency files..."
            clean_dependency_files "build_wasm"
            echo "Dependency files cleaned from WASM build directory."
            exit 0
            ;;
        --jobs=*|-j=*)
            PARALLEL_JOBS="${1#*=}"
            shift
            ;;
        --jobs|-j)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --wasi-sdk=*)
            WASI_SDK_PATH="${1#*=}"
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--debug] [--force] [--jobs=N] [--clean-deps] [--wasi-sdk=PATH]"
            echo ""
            echo "WASM Compilation Script for Lambda Project"
            echo "Compiles lambda/input, lambda/format, and lambda/validator C files into WASM using WASI SDK"
            echo ""
            echo "Arguments:"
            echo "  --debug, -d           Build debug version with debug symbols"
            echo "  --force, -f           Force rebuild all files (disable incremental compilation)"
            echo "  --jobs=N, -j N        Number of parallel compilation jobs (default: auto-detect)"
            echo "  --clean-deps          Clean dependency files (.d files) and exit"
            echo "  --wasi-sdk=PATH       Path to WASI SDK (default: /opt/wasi-sdk)"
            echo ""
            echo "Environment Variables:"
            echo "  WASI_SDK_PATH         Path to WASI SDK installation"
            echo ""
            echo "Examples:"
            echo "  $0                    # Native WASM compilation (incremental)"
            echo "  $0 --debug            # Debug WASM build"
            echo "  $0 --force            # Force full rebuild"
            echo "  $0 --jobs=4           # Compile with 4 parallel jobs"
            echo "  $0 --wasi-sdk=/usr/local/wasi-sdk  # Use custom WASI SDK path"
            exit 0
            ;;
        --*)
            echo "Error: Unknown option $1"
            echo "Use --help for usage information"
            exit 1
            ;;
        *)
            echo "Error: Unexpected argument $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check if WASI SDK is available
if [ ! -d "$WASI_SDK_PATH" ]; then
    echo -e "${RED}Error: WASI SDK not found at $WASI_SDK_PATH${RESET}"
    echo "Please install WASI SDK or set WASI_SDK_PATH environment variable"
    echo "Download from: https://github.com/WebAssembly/wasi-sdk/releases"
    exit 1
fi

CC="$WASI_SDK_PATH/bin/clang"
CXX="$WASI_SDK_PATH/bin/clang++"

if [ ! -x "$CC" ]; then
    echo -e "${RED}Error: WASI clang compiler not found at $CC${RESET}"
    exit 1
fi

echo -e "${GREEN}Using WASI SDK: $WASI_SDK_PATH${RESET}"
echo "Using C compiler: $CC"

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file '$CONFIG_FILE' not found!"
    exit 1
fi

# Function to extract values from JSON using jq or basic parsing
get_json_value() {
    local key="$1"
    local file="$2"
    
    if command -v jq >/dev/null 2>&1; then
        jq -r ".$key // empty" "$file" 2>/dev/null
    else
        # Fallback to grep/sed for basic JSON parsing
        grep "\"$key\"" "$file" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g'
    fi
}

# Function to extract array values from JSON
get_json_array() {
    local key="$1"
    local file="$2"
    
    if command -v jq >/dev/null 2>&1; then
        jq -r ".$key[]? // empty" "$file" 2>/dev/null
    else
        # Fallback parsing for arrays
        sed -n '/"'$key'":/,/]/p' "$file" | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
    fi
}

# Function to collect source files from directories
collect_source_files_from_dirs() {
    local config_file="$1"
    local collected_files=()
    
    # Get source_dirs array
    local source_dirs_list
    if command -v jq >/dev/null 2>&1; then
        source_dirs_list=$(jq -r ".source_dirs[]? // empty" "$config_file" 2>/dev/null)
    else
        source_dirs_list=$(get_json_array "source_dirs" "$config_file")
    fi
    
    # Process each directory
    while IFS= read -r dir; do
        if [ -n "$dir" ] && [ -d "$dir" ]; then
            # Find all C source files in the directory (recursively)
            while IFS= read -r -d '' file; do
                collected_files+=("$file")
            done < <(find "$dir" -type f -name "*.c" -print0 2>/dev/null)
        elif [ -n "$dir" ]; then
            echo "Warning: source_dirs entry '$dir' is not a valid directory" >&2
        fi
    done <<< "$source_dirs_list"
    
    # Output collected files (one per line)
    printf '%s\n' "${collected_files[@]}"
}

# Function to check if source file needs recompilation
needs_recompilation() {
    local source_file="$1"
    local object_file="$2"
    local dep_file="${object_file%.o}.d"
    
    # If force rebuild is enabled, always recompile
    if [ "$FORCE_REBUILD" = true ]; then
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
    
    # Check dependencies from .d file if it exists
    if [ -f "$dep_file" ]; then
        local deps=$(sed -e 's/\\$//' -e 's/^[^:]*://' "$dep_file" | tr -s ' ' '\n' | sed '/^$/d' | sort -u)
        
        # Check if any dependency is newer than the object file
        while IFS= read -r dep; do
            if [ -n "$dep" ] && [ "$dep" != "$object_file" ] && [ -f "$dep" ] && [ "$dep" -nt "$object_file" ]; then
                echo "Dependency changed: $dep"
                return 0
            fi
        done <<< "$deps"
        
        return 1
    fi
    
    # No recompilation needed
    return 1
}

# Function to check if linking is needed
needs_linking() {
    local output_file="$1"
    local object_files="$2"
    
    # If force rebuild is enabled, always link
    if [ "$FORCE_REBUILD" = true ]; then
        return 0
    fi
    
    # If output file doesn't exist, link
    if [ ! -f "$output_file" ]; then
        return 0
    fi
    
    # Check if any object file is newer than output
    for obj_file in $object_files; do
        if [ -f "$obj_file" ] && [ "$obj_file" -nt "$output_file" ]; then
            return 0
        fi
    done
    
    return 1
}

# Read configuration
BUILD_DIR=$(get_json_value "build_dir" "$CONFIG_FILE")
OUTPUT=$(get_json_value "output" "$CONFIG_FILE")
DEBUG=$(get_json_value "debug" "$CONFIG_FILE")

# Validate required fields
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="build_wasm"  # Default fallback
fi

if [ -z "$OUTPUT" ]; then
    OUTPUT="lambda.wasm"  # Default fallback
fi

# Override debug setting if --debug is specified
if [ "$DEBUG_BUILD" = true ]; then
    DEBUG="true"
fi

echo "Configuration loaded from: $CONFIG_FILE"
echo "Build directory: $BUILD_DIR"
echo "Output file: $OUTPUT"

# Set up parallel compilation
if [ -z "$PARALLEL_JOBS" ]; then
    # Auto-detect number of CPU cores
    if command -v nproc >/dev/null 2>&1; then
        PARALLEL_JOBS=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        PARALLEL_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo "1")
    else
        PARALLEL_JOBS=1
    fi
fi

# Limit to reasonable maximum
if [ "$PARALLEL_JOBS" -gt 8 ]; then
    PARALLEL_JOBS=8
fi

echo "Parallel jobs: $PARALLEL_JOBS"

# Create build directory
mkdir -p "$BUILD_DIR"

# Build compiler flags
INCLUDES=""
LIBS=""
WARNINGS=""
FLAGS=""
LINKER_FLAGS=""

# Process libraries
if command -v jq >/dev/null 2>&1; then
    while IFS= read -r lib_info; do
        name=$(echo "$lib_info" | jq -r '.name')
        include=$(echo "$lib_info" | jq -r '.include // empty')
        lib=$(echo "$lib_info" | jq -r '.lib // empty')
        
        [ -n "$include" ] && INCLUDES="$INCLUDES -I$include"
        [ -n "$lib" ] && LIBS="$LIBS $lib"
    done < <(jq -c '.libraries[]?' "$CONFIG_FILE")
    
    # Process warnings
    while IFS= read -r warning; do
        [ -n "$warning" ] && WARNINGS="$WARNINGS -Werror=$warning"
    done < <(get_json_array "warnings" "$CONFIG_FILE")
    
    # Process flags
    while IFS= read -r flag; do
        [ -n "$flag" ] && FLAGS="$FLAGS -$flag"
    done < <(get_json_array "flags" "$CONFIG_FILE")
    
    # Process linker flags
    while IFS= read -r flag; do
        [ -n "$flag" ] && LINKER_FLAGS="$LINKER_FLAGS -$flag"
    done < <(get_json_array "linker_flags" "$CONFIG_FILE")
    
    # Get all source files
    SOURCE_FILES_ARRAY=()
    
    # Read source_files entries
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(jq -r '.source_files[]?' "$CONFIG_FILE" 2>/dev/null)
    
    # Collect additional source files from source_dirs
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(collect_source_files_from_dirs "$CONFIG_FILE")
else
    echo "Warning: jq not found, using fallback parsing"
    # Fallback configuration for WASM
    INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c"
    LIBS="lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a"
    WARNINGS="-Werror=format -Werror=multichar"
    FLAGS="-fms-extensions -O2 -DWASM_BUILD -D_POSIX_C_SOURCE=200809L"
    LINKER_FLAGS="-Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined -nostdlib"
    
    # Parse source files
    SOURCE_FILES_ARRAY=()
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(get_json_array "source_files" "$CONFIG_FILE")
    
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(collect_source_files_from_dirs "$CONFIG_FILE")
fi

# Add debug flag if enabled
if [ "$DEBUG" = "true" ]; then
    FLAGS="$FLAGS -g"
fi

# Add WASI-specific system root
WASI_SYSROOT="$WASI_SDK_PATH/share/wasi-sysroot"
FLAGS="$FLAGS --sysroot=$WASI_SYSROOT"

echo "Source files count: ${#SOURCE_FILES_ARRAY[@]}"
if [ "$DEBUG_BUILD" = true ]; then
    echo -e "${YELLOW}Debug build: enabled${RESET}"
fi
if [ "$FORCE_REBUILD" = true ]; then
    echo -e "${YELLOW}Build mode: Full rebuild (--force)${RESET}"
else
    echo "Build mode: Incremental"
fi
echo

# Function to compile a single file
compile_single_file() {
    local source="$1"
    local obj_file="$2"
    
    echo "Compiling: $source -> $obj_file"
    $CC $INCLUDES -MMD -MP -c "$source" -o "$obj_file" $WARNINGS $FLAGS 2>&1
}

# Pre-scan all files to determine what needs compilation
echo "Scanning files for changes..."
FILES_TO_COMPILE=()
FILES_TO_COMPILE_OBJ=()
OBJECT_FILES=""
compilation_success=true
files_compiled=0
files_skipped=0

# Scan all source files
for source in "${SOURCE_FILES_ARRAY[@]}"; do
    if [ -f "$source" ]; then
        obj_name=$(basename "$source" .c)
        obj_file="$BUILD_DIR/${obj_name}.o"
        OBJECT_FILES="$OBJECT_FILES $obj_file"
        
        if needs_recompilation "$source" "$obj_file"; then
            FILES_TO_COMPILE+=("$source")
            FILES_TO_COMPILE_OBJ+=("$obj_file")
            files_compiled=$((files_compiled + 1))
        else
            echo "Up-to-date: $source"
            files_skipped=$((files_skipped + 1))
        fi
    else
        echo "Warning: Source file '$source' not found"
        compilation_success=false
    fi
done

# Perform compilation (parallel if multiple files)
output=""
if [ ${#FILES_TO_COMPILE[@]} -gt 0 ]; then
    if [ ${#FILES_TO_COMPILE[@]} -gt 1 ] && [ "$PARALLEL_JOBS" -gt 1 ]; then
        echo "Compiling ${#FILES_TO_COMPILE[@]} files in parallel (max $PARALLEL_JOBS jobs)..."
        
        # Use background processes for parallel compilation
        declare -a pids
        job_count=0
        
        for ((i=0; i<${#FILES_TO_COMPILE[@]}; i++)); do
            source="${FILES_TO_COMPILE[$i]}"
            obj_file="${FILES_TO_COMPILE_OBJ[$i]}"
            
            (
                compile_result=$(compile_single_file "$source" "$obj_file")
                echo "$compile_result" > "${obj_file}.compile_log"
                echo $? > "${obj_file}.compile_status"
            ) &
            
            pids+=($!)
            job_count=$((job_count + 1))
            
            # Limit parallel jobs
            if [ $job_count -ge $PARALLEL_JOBS ]; then
                wait ${pids[0]}
                pids=("${pids[@]:1}")
                job_count=$((job_count - 1))
            fi
        done
        
        # Wait for remaining jobs
        for pid in "${pids[@]}"; do
            wait $pid
        done
        
        # Collect results
        for ((i=0; i<${#FILES_TO_COMPILE[@]}; i++)); do
            obj_file="${FILES_TO_COMPILE_OBJ[$i]}"
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
        for ((i=0; i<${#FILES_TO_COMPILE[@]}; i++)); do
            source="${FILES_TO_COMPILE[$i]}"
            obj_file="${FILES_TO_COMPILE_OBJ[$i]}"
            
            compile_output=$(compile_single_file "$source" "$obj_file")
            if [ $? -ne 0 ]; then
                compilation_success=false
            fi
            if [ -n "$compile_output" ]; then
                output="$output\n$compile_output"
            fi
        done
    fi
else
    echo "No files need compilation."
fi

# Link final WASM module only if compilation was successful
linking_performed=false
if [ "$compilation_success" = true ]; then
    if needs_linking "$OUTPUT" "$OBJECT_FILES"; then
        echo "Linking: $OUTPUT"
        link_output=$($CC $OBJECT_FILES $LIBS $LINKER_FLAGS -o "$OUTPUT" 2>&1)
        if [ $? -ne 0 ]; then
            compilation_success=false
        fi
        if [ -n "$link_output" ]; then
            output="$output\n$link_output"
        fi
        linking_performed=true
    else
        echo "Up-to-date: $OUTPUT"
    fi
fi

# Output captured messages
if [ -n "$output" ]; then
    echo -e "$output"
fi

# Count errors and warnings
num_errors=$(echo "$output" | grep -c "error:" || true)
num_warnings=$(echo "$output" | grep -c "warning:" || true)

# Print summary
echo
echo -e "${YELLOW}WASM Build Summary:${RESET}"
if [ "$num_errors" -gt 0 ]; then
    echo -e "${RED}Errors:   $num_errors${RESET}"
else
    echo "Errors:   $num_errors"
fi
echo -e "${YELLOW}Warnings: $num_warnings${RESET}"
echo "Files compiled: $files_compiled"
echo "Files up-to-date: $files_skipped"
if [ "$linking_performed" = true ]; then
    echo "Linking: performed"
else
    echo "Linking: skipped (up-to-date)"
fi

# Show dependency tracking info
dep_files_count=$(find "$BUILD_DIR" -name "*.d" 2>/dev/null | wc -l | tr -d ' ')
if [ "$dep_files_count" -gt 0 ]; then
    echo "Dependency tracking: $dep_files_count .d files"
fi

# Final build status
if [ "$compilation_success" = true ]; then
    echo -e "${GREEN}WASM build successful: $OUTPUT${RESET}"
    
    # Show WASM file info
    if [ -f "$OUTPUT" ]; then
        echo "WASM file information:"
        ls -lh "$OUTPUT" 2>/dev/null || echo "Could not get file size"
        if command -v file >/dev/null 2>&1; then
            file "$OUTPUT" 2>/dev/null || echo "Could not determine file type"
        fi
        if command -v wasm-objdump >/dev/null 2>&1; then
            echo "WASM exports:"
            wasm-objdump -x "$OUTPUT" | grep -A 10 "Export\[" 2>/dev/null || echo "Could not list exports"
        fi
    fi
    
    exit 0
else
    echo -e "${RED}WASM build failed${RESET}"
    exit 1
fi
