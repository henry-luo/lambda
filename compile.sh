#!/bin/bash

# Usage: ./compile.sh [config_file] [--platform=PLATFORM]
#        ./compile.sh --help

# Configuration file default
CONFIG_FILE="build_lambda_config.json"
PLATFORM=""

# Parse command line arguments
FORCE_REBUILD=false
PARALLEL_JOBS=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --platform=*)
            PLATFORM="${1#*=}"
            shift
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --force|-f)
            FORCE_REBUILD=true
            shift
            ;;
        --jobs=*|-j=*)
            PARALLEL_JOBS="${1#*=}"
            shift
            ;;
        --jobs|-j)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [config_file] [--platform=PLATFORM] [--force] [--jobs=N]"
            echo ""
            echo "Arguments:"
            echo "  config_file           JSON configuration file (default: build_lambda_config.json)"
            echo "  --platform=PLATFORM   Target platform for cross-compilation"
            echo "  --force, -f           Force rebuild all files (disable incremental compilation)"
            echo "  --jobs=N, -j N        Number of parallel compilation jobs (default: auto-detect)"
            echo ""
            echo "Available platforms:"
            echo "  windows               Cross-compile for Windows using MinGW"
            echo "  (none)                Native compilation for current platform (e.g., macOS, Linux)"
            echo ""
            echo "Configuration files:"
            echo "  build_lambda_config.json    Compile lambda project"
            echo "  build_radiant_config.json   Compile radiant project"
            echo ""
            echo "Examples:"
            echo "  $0                                      # Native lambda compilation (incremental)"
            echo "  $0 --force                              # Native lambda compilation (full rebuild)"
            echo "  $0 --jobs=4                             # Native lambda compilation with 4 parallel jobs"
            echo "  $0 build_radiant_config.json            # Native radiant compilation (incremental)"
            echo "  $0 --platform=windows                   # Cross-compile lambda for Windows (incremental)"
            echo "  $0 --platform=windows --force           # Cross-compile lambda for Windows (full rebuild)"
            exit 0
            ;;
        --*)
            echo "Error: Unknown option $1"
            echo "Use --help for usage information"
            exit 1
            ;;
        *)
            CONFIG_FILE="$1"
            shift
            ;;
    esac
done

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file '$CONFIG_FILE' not found!"
    exit 1
fi

# Function to extract values from JSON using jq or basic parsing
get_json_value() {
    local key="$1"
    local file="$2"
    local platform_prefix="$3"
    
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific value first, fallback to default
            local platform_value=$(jq -r ".platforms.$platform_prefix.$key // empty" "$file" 2>/dev/null)
            if [ -n "$platform_value" ] && [ "$platform_value" != "null" ]; then
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
            # Try platform-specific value first
            local platform_value=$(grep -A 20 "\"platforms\"" "$file" | grep -A 10 "\"$platform_prefix\"" | grep "\"$key\"" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g')
            if [ -n "$platform_value" ]; then
                echo "$platform_value"
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
            # Try platform-specific array first, fallback to default
            local platform_array=$(jq -r ".platforms.$platform_prefix.$key[]? // empty" "$file" 2>/dev/null)
            if [ -n "$platform_array" ]; then
                echo "$platform_array"
            else
                jq -r ".$key[]? // empty" "$file" 2>/dev/null
            fi
        else
            jq -r ".$key[]? // empty" "$file" 2>/dev/null
        fi
    else
        # Fallback parsing for arrays
        if [ -n "$platform_prefix" ]; then
            # Try platform-specific array first
            local platform_section=$(sed -n '/"platforms"/,/}/p' "$file" | sed -n '/"'$platform_prefix'"/,/}/p')
            if echo "$platform_section" | grep -q "\"$key\""; then
                echo "$platform_section" | sed -n '/"'$key'":/,/]/p' | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
                return
            fi
        fi
        sed -n '/"'$key'":/,/]/p' "$file" | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
    fi
}

# Global cache for header file timestamps (populated once)
NEWEST_HEADER_TIME=""
INCLUDE_DIRS_ARRAY=()

# Function to initialize header file cache
init_header_cache() {
    # Extract include directories once
    if [ -n "$INCLUDES" ]; then
        while IFS= read -r include_dir; do
            [ -d "$include_dir" ] && INCLUDE_DIRS_ARRAY+=("$include_dir")
        done < <(echo "$INCLUDES" | grep -o '\-I[^ ]*' | sed 's/^-I//')
    fi
    
    # Find the newest header file timestamp across all include directories
    if [ ${#INCLUDE_DIRS_ARRAY[@]} -gt 0 ]; then
        local newest_header=""
        # Use find with -printf for better performance (if available)
        if find "${INCLUDE_DIRS_ARRAY[@]}" -name "*.h" -printf '%T@ %p\n' 2>/dev/null | head -1 >/dev/null 2>&1; then
            newest_header=$(find "${INCLUDE_DIRS_ARRAY[@]}" -name "*.h" -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -1 | cut -d' ' -f2-)
        else
            # Fallback for systems without -printf
            newest_header=$(find "${INCLUDE_DIRS_ARRAY[@]}" -name "*.h" 2>/dev/null | xargs ls -t 2>/dev/null | head -1)
        fi
        
        if [ -n "$newest_header" ] && [ -f "$newest_header" ]; then
            NEWEST_HEADER_TIME=$(stat -f "%m" "$newest_header" 2>/dev/null || stat -c "%Y" "$newest_header" 2>/dev/null)
        fi
    fi
}

# Optimized function to check if source file needs recompilation
needs_recompilation() {
    local source_file="$1"
    local object_file="$2"
    
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
    
    # Quick header check using cached timestamp
    if [ -n "$NEWEST_HEADER_TIME" ]; then
        local obj_time=$(stat -f "%m" "$object_file" 2>/dev/null || stat -c "%Y" "$object_file" 2>/dev/null)
        if [ -n "$obj_time" ] && [ "$NEWEST_HEADER_TIME" -gt "$obj_time" ]; then
            return 0
        fi
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
    
    # Get output file timestamp once
    local output_time=$(stat -f "%m" "$output_file" 2>/dev/null || stat -c "%Y" "$output_file" 2>/dev/null)
    if [ -z "$output_time" ]; then
        return 0  # Can't get timestamp, safer to link
    fi
    
    # Check object files (batch processing)
    for obj_file in $object_files; do
        if [ -f "$obj_file" ]; then
            local obj_time=$(stat -f "%m" "$obj_file" 2>/dev/null || stat -c "%Y" "$obj_file" 2>/dev/null)
            if [ -n "$obj_time" ] && [ "$obj_time" -gt "$output_time" ]; then
                return 0
            fi
        fi
    done
    
    # Check static libraries (batch processing)
    for lib_file in $LIBS; do
        if [ -f "$lib_file" ]; then
            local lib_time=$(stat -f "%m" "$lib_file" 2>/dev/null || stat -c "%Y" "$lib_file" 2>/dev/null)
            if [ -n "$lib_time" ] && [ "$lib_time" -gt "$output_time" ]; then
                return 0
            fi
        fi
    done
    
    # No linking needed
    return 1
}

# Read configuration with platform override support
BUILD_DIR=$(get_json_value "build_dir" "$CONFIG_FILE" "$PLATFORM")
OUTPUT=$(get_json_value "output" "$CONFIG_FILE" "$PLATFORM")
DEBUG=$(get_json_value "debug" "$CONFIG_FILE" "$PLATFORM")
CROSS_COMPILE=$(get_json_value "cross_compile" "$CONFIG_FILE" "$PLATFORM")
TARGET_TRIPLET=$(get_json_value "target_triplet" "$CONFIG_FILE" "$PLATFORM")

# Validate required fields
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="build"  # Default fallback
fi

if [ -z "$OUTPUT" ]; then
    OUTPUT="lambda.exe"  # Default fallback
fi

# Set cross-compilation if platform is specified
if [ -n "$PLATFORM" ]; then
    if [ "$PLATFORM" = "windows" ]; then
        CROSS_COMPILE="true"
        if [ -z "$TARGET_TRIPLET" ]; then
            TARGET_TRIPLET="x86_64-w64-mingw32"
        fi
    fi
fi

# Set compilers based on cross-compilation target
if [ "$CROSS_COMPILE" = "true" ] && [ -n "$TARGET_TRIPLET" ]; then
    echo "Cross-compiling for target: $TARGET_TRIPLET"
    CC="${TARGET_TRIPLET}-gcc"
    CXX="${TARGET_TRIPLET}-g++"
    
    # Check if cross-compiler exists
    if ! command -v "$CC" >/dev/null 2>&1; then
        echo "Error: Cross-compiler '$CC' not found!"
        echo "Install it with: brew install mingw-w64"
        exit 1
    fi
    
    if ! command -v "$CXX" >/dev/null 2>&1; then
        echo "Error: Cross-compiler '$CXX' not found!"
        echo "Install it with: brew install mingw-w64"
        exit 1
    fi
else
    CC="clang"
    CXX="clang++"
fi

echo "Using C compiler: $CC"
echo "Using C++ compiler: $CXX"

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
LINK_LIBS=""
WARNINGS=""
FLAGS=""

# Process libraries with platform override support
if command -v jq >/dev/null 2>&1; then
    # Extract library information using jq
    if [ -n "$PLATFORM" ]; then
        # Use platform-specific libraries if available
        while IFS= read -r lib_info; do
            name=$(echo "$lib_info" | jq -r '.name')
            include=$(echo "$lib_info" | jq -r '.include // empty')
            lib=$(echo "$lib_info" | jq -r '.lib // empty')
            link=$(echo "$lib_info" | jq -r '.link // "static"')
            
            [ -n "$include" ] && INCLUDES="$INCLUDES -I$include"
            
            if [ "$link" = "static" ]; then
                [ -n "$lib" ] && LIBS="$LIBS $lib"
            else
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS -L$lib -l$name"
            fi
        done < <(jq -c ".platforms.$PLATFORM.libraries[]? // .libraries[]?" "$CONFIG_FILE")
    else
        # Use default libraries
        while IFS= read -r lib_info; do
            name=$(echo "$lib_info" | jq -r '.name')
            include=$(echo "$lib_info" | jq -r '.include // empty')
            lib=$(echo "$lib_info" | jq -r '.lib // empty')
            link=$(echo "$lib_info" | jq -r '.link // "static"')
            
            [ -n "$include" ] && INCLUDES="$INCLUDES -I$include"
            
            if [ "$link" = "static" ]; then
                [ -n "$lib" ] && LIBS="$LIBS $lib"
            else
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS -L$lib -l$name"
            fi
        done < <(jq -c '.libraries[]?' "$CONFIG_FILE")
    fi
    
    # Process warnings with platform override
    while IFS= read -r warning; do
        [ -n "$warning" ] && WARNINGS="$WARNINGS -Werror=$warning"
    done < <(get_json_array "warnings" "$CONFIG_FILE" "$PLATFORM")
    
    # Process flags with platform override
    while IFS= read -r flag; do
        [ -n "$flag" ] && FLAGS="$FLAGS -$flag"
    done < <(get_json_array "flags" "$CONFIG_FILE" "$PLATFORM")
    
    # Process linker flags with platform override
    LINKER_FLAGS=""
    while IFS= read -r flag; do
        [ -n "$flag" ] && LINKER_FLAGS="$LINKER_FLAGS -$flag"
    done < <(get_json_array "linker_flags" "$CONFIG_FILE" "$PLATFORM")
    
    # Get source files (using arrays compatible with older bash)
    SOURCE_FILES_ARRAY=()
    CPP_FILES_ARRAY=()
    
    # Read source files into array
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(jq -r '.source_files[]?' "$CONFIG_FILE" 2>/dev/null)
    
    # Read cpp files into array
    while IFS= read -r line; do
        [ -n "$line" ] && CPP_FILES_ARRAY+=("$line")
    done < <(jq -r '.cpp_files[]?' "$CONFIG_FILE" 2>/dev/null)
else
    # Fallback without jq - basic parsing
    if [ "$CROSS_COMPILE" = "true" ] || [ "$PLATFORM" = "windows" ]; then
        echo "Warning: jq not found, using fallback parsing for cross-compilation"
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -Iwindows-deps/include"
        LIBS="lambda/tree-sitter/libtree-sitter-windows.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a windows-deps/lib/libmir.a windows-deps/lib/libzlog.a windows-deps/lib/liblexbor_static.a windows-deps/lib/libgmp.a"
        LINK_LIBS=""
        WARNINGS="-Wformat -Wincompatible-pointer-types -Wmultichar"
        FLAGS="-fms-extensions -static -DCROSS_COMPILE -D_WIN32"
        LINKER_FLAGS="-static-libgcc -static-libstdc++"
    else
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -I/usr/local/include -I/opt/homebrew/include"
        LIBS="lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a /usr/local/lib/liblexbor_static.a"
        LINK_LIBS="-L/opt/homebrew/lib -lgmp"
        WARNINGS="-Werror=format -Werror=incompatible-pointer-types -Werror=multichar"
        FLAGS="-fms-extensions -pedantic -fcolor-diagnostics"
        LINKER_FLAGS=""
    fi
    
    # Parse source files into arrays
    SOURCE_FILES_ARRAY=()
    CPP_FILES_ARRAY=()
    
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(get_json_array "source_files" "$CONFIG_FILE")
    
    while IFS= read -r line; do
        [ -n "$line" ] && CPP_FILES_ARRAY+=("$line")
    done < <(get_json_array "cpp_files" "$CONFIG_FILE")
fi

# Add debug flag if enabled
if [ "$DEBUG" = "true" ]; then
    FLAGS="$FLAGS -g"
fi

# Initialize header file cache for faster incremental builds
if [ "$FORCE_REBUILD" != true ]; then
    echo "Initializing incremental build cache..."
    init_header_cache
    if [ -n "$NEWEST_HEADER_TIME" ]; then
        echo "Header cache: Found newest header timestamp"
    else
        echo "Header cache: No headers found or timestamps unavailable"
    fi
fi

# Debug output
echo "Configuration loaded from: $CONFIG_FILE"
if [ -n "$PLATFORM" ]; then
    echo "Target platform: $PLATFORM"
fi
echo "Cross-compilation: $CROSS_COMPILE"
if [ "$FORCE_REBUILD" = true ]; then
    echo "Build mode: Full rebuild (--force)"
else
    echo "Build mode: Incremental"
fi
echo "Build directory: $BUILD_DIR"
echo "Output executable: $OUTPUT"
echo "Source files count: ${#SOURCE_FILES_ARRAY[@]}"
echo "C++ files count: ${#CPP_FILES_ARRAY[@]}"
echo

# Function to compile a single file (for parallel execution)
compile_single_file() {
    local source="$1"
    local obj_file="$2"
    local compiler="$3"
    local warnings="$4"
    local flags="$5"
    
    echo "Compiling: $source -> $obj_file"
    $compiler $INCLUDES -c "$source" -o "$obj_file" $warnings $flags 2>&1
}

# Pre-scan all files to determine what needs compilation
echo "Scanning files for changes..."
FILES_TO_COMPILE=()
FILES_TO_COMPILE_OBJ=()
FILES_TO_COMPILE_COMPILER=()
FILES_TO_COMPILE_WARNINGS=()
FILES_TO_COMPILE_FLAGS=()

# Initialize compilation tracking variables
output=""
OBJECT_FILES=""
compilation_success=true
files_compiled=0
files_skipped=0

# Scan C source files
for source in "${SOURCE_FILES_ARRAY[@]}"; do
    if [ -f "$source" ]; then
        obj_name=$(basename "$source" | sed 's/\.[^.]*$//')
        obj_file="$BUILD_DIR/${obj_name}.o"
        OBJECT_FILES="$OBJECT_FILES $obj_file"
        
        if needs_recompilation "$source" "$obj_file"; then
            FILES_TO_COMPILE+=("$source")
            FILES_TO_COMPILE_OBJ+=("$obj_file")
            FILES_TO_COMPILE_COMPILER+=("$CC")
            FILES_TO_COMPILE_WARNINGS+=("$WARNINGS")
            FILES_TO_COMPILE_FLAGS+=("$FLAGS")
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

# Scan C++ files
if [ ${#CPP_FILES_ARRAY[@]} -gt 0 ]; then
    for cpp_file in "${CPP_FILES_ARRAY[@]}"; do
        if [ -f "$cpp_file" ]; then
            cpp_obj_name=$(basename "$cpp_file" | sed 's/\.[^.]*$//')
            cpp_obj_file="$BUILD_DIR/${cpp_obj_name}.o"
            OBJECT_FILES="$OBJECT_FILES $cpp_obj_file"
            
            if needs_recompilation "$cpp_file" "$cpp_obj_file"; then
                # Use C++ specific flags
                CPP_WARNINGS=$(echo "$WARNINGS" | sed 's/-Werror=incompatible-pointer-types//g' | sed 's/-Wincompatible-pointer-types//g')
                CPP_FLAGS="$FLAGS"
                if [ "$CROSS_COMPILE" = "true" ]; then
                    CPP_FLAGS="$CPP_FLAGS -std=c++11 -fpermissive -Wno-fpermissive"
                fi
                
                FILES_TO_COMPILE+=("$cpp_file")
                FILES_TO_COMPILE_OBJ+=("$cpp_obj_file")
                FILES_TO_COMPILE_COMPILER+=("$CXX")
                FILES_TO_COMPILE_WARNINGS+=("$CPP_WARNINGS")
                FILES_TO_COMPILE_FLAGS+=("$CPP_FLAGS")
                files_compiled=$((files_compiled + 1))
            else
                echo "Up-to-date: $cpp_file"
                files_skipped=$((files_skipped + 1))
            fi
        else
            echo "Warning: C++ file '$cpp_file' not found"
            compilation_success=false
        fi
    done
fi

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
            compiler="${FILES_TO_COMPILE_COMPILER[$i]}"
            warnings="${FILES_TO_COMPILE_WARNINGS[$i]}"
            flags="${FILES_TO_COMPILE_FLAGS[$i]}"
            
            # Start compilation in background
            (
                compile_result=$(compile_single_file "$source" "$obj_file" "$compiler" "$warnings" "$flags")
                echo "$compile_result" > "${obj_file}.compile_log"
                echo $? > "${obj_file}.compile_status"
            ) &
            
            pids+=($!)
            job_count=$((job_count + 1))
            
            # Limit parallel jobs
            if [ $job_count -ge $PARALLEL_JOBS ]; then
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
        # Sequential compilation for single file or when parallel is disabled
        for ((i=0; i<${#FILES_TO_COMPILE[@]}; i++)); do
            source="${FILES_TO_COMPILE[$i]}"
            obj_file="${FILES_TO_COMPILE_OBJ[$i]}"
            compiler="${FILES_TO_COMPILE_COMPILER[$i]}"
            warnings="${FILES_TO_COMPILE_WARNINGS[$i]}"
            flags="${FILES_TO_COMPILE_FLAGS[$i]}"
            
            compile_output=$(compile_single_file "$source" "$obj_file" "$compiler" "$warnings" "$flags")
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

# Link final executable only if compilation was successful
linking_performed=false
if [ "$compilation_success" = true ]; then
    if needs_linking "$OUTPUT" "$OBJECT_FILES"; then
        echo "Linking: $OUTPUT"
        link_output=$($CXX $OBJECT_FILES $LIBS $LINK_LIBS $LINKER_FLAGS -o "$OUTPUT" 2>&1)
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

# Output the captured messages
echo -e "$output"

# Count errors and warnings (ignores color codes)
num_errors=$(echo "$output" | grep -c "error:")
num_warnings=$(echo "$output" | grep -c "warning:")

# Print summary with optional coloring
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
RESET="\033[0m"

echo
echo -e "${YELLOW}Summary:${RESET}"
if [ "$num_errors" -gt 0 ]; then
    echo -e "${RED}Errors:   $num_errors${RESET}"
else
    echo -e "Errors:   $num_errors"
fi
echo -e "${YELLOW}Warnings: $num_warnings${RESET}"
echo "Files compiled: $files_compiled"
echo "Files up-to-date: $files_skipped"
if [ "$linking_performed" = true ]; then
    echo "Linking: performed"
else
    echo "Linking: skipped (up-to-date)"
fi

# Final build status
if [ "$compilation_success" = true ]; then
    echo -e "${GREEN}Build successful: $OUTPUT${RESET}"
    
    # Show file info for cross-compiled binaries
    if [ "$CROSS_COMPILE" = "true" ]; then
        echo "File type information:"
        file "$OUTPUT" 2>/dev/null || echo "file command not available"
    fi
    
    exit 0
else
    echo -e "${RED}Build failed${RESET}"
    exit 1
fi


