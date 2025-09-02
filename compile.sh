#!/bin/bash

# Usage: ./compile.sh [config_file] [--platform=PLATFORM]
#        ./compile.sh --help

# Source shared build utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/utils/build_utils.sh" ]; then
    source "$SCRIPT_DIR/utils/build_utils.sh"
elif [ -f "$SCRIPT_DIR/lib/build_utils.sh" ]; then
    source "$SCRIPT_DIR/lib/build_utils.sh"
else
    echo "Warning: build_utils.sh not found, using legacy functions"
fi

# Source shared build core functions  
if [ -f "$SCRIPT_DIR/utils/build_core.sh" ]; then
    source "$SCRIPT_DIR/utils/build_core.sh"
elif [ -f "$SCRIPT_DIR/lib/build_core.sh" ]; then
    source "$SCRIPT_DIR/lib/build_core.sh"
else
    echo "Warning: build_core.sh not found, unified functions unavailable"
fi

# Configuration file default
CONFIG_FILE="build_lambda_config.json"
PLATFORM=""

# Function to clean dependency files (.d files)
clean_dependency_files() {
    local build_dir="${1:-build}"
    if [ -d "$build_dir" ]; then
        echo "Cleaning dependency files in $build_dir..."
        find "$build_dir" -name "*.d" -delete 2>/dev/null || true
    fi
}

# Parse command line arguments
FORCE_REBUILD=false
PARALLEL_JOBS=""
DEBUG_BUILD=false
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
        --debug|-d)
            DEBUG_BUILD=true
            PLATFORM="debug"
            shift
            ;;
        --force|-f)
            FORCE_REBUILD=true
            shift
            ;;
        --clean-deps)
            # Clean standard build directories
            echo "Cleaning dependency files..."
            clean_dependency_files "build"
            clean_dependency_files "build_windows" 
            clean_dependency_files "build_debug"
            echo "Dependency files cleaned from standard build directories."
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
        --help|-h)
            echo "Usage: $0 [config_file] [--platform=PLATFORM] [--debug] [--force] [--jobs=N] [--clean-deps]"
            echo ""
            echo "Arguments:"
            echo "  config_file           JSON configuration file (default: build_lambda_config.json)"
            echo "  --platform=PLATFORM   Target platform for cross-compilation"
            echo "  --debug, -d           Build debug version with AddressSanitizer"
            echo "  --force, -f           Force rebuild all files (disable incremental compilation)"
            echo "  --jobs=N, -j N        Number of parallel compilation jobs (default: auto-detect)"
            echo "  --clean-deps          Clean dependency files (.d files) and exit"
            echo ""
            echo "Available platforms:"
            echo "  windows               Cross-compile for Windows using MinGW"
            echo "  debug                 Debug build with AddressSanitizer (same as --debug)"
            echo "  (none)                Native compilation for current platform (e.g., macOS, Linux)"
            echo ""
            echo "Configuration files:"
            echo "  build_lambda_config.json    Compile lambda project"
            echo "  build_radiant_config.json   Compile radiant project"
            echo ""
            echo "Configuration file format:"
            echo "  source_files              Array of source files to compile (C and C++ auto-detected)"
            echo "  source_dirs               Array of directories to recursively scan for source files"
            echo "                            (automatically detects .c, .cpp, .cc, .cxx, .c++ files)"
            echo "                            File types are auto-detected and compiled with appropriate compilers"
            echo ""
            echo "Examples:"
            echo "  $0                                      # Native lambda compilation (incremental with .d files)"
            echo "  $0 --debug                              # Debug build with AddressSanitizer"
            echo "  $0 --force                              # Native lambda compilation (full rebuild)"
            echo "  $0 --clean-deps                         # Clean dependency files"
            echo "  $0 --jobs=4                             # Native lambda compilation with 4 parallel jobs"
            echo "  $0 build_radiant_config.json            # Native radiant compilation (incremental)"
            echo "  $0 --platform=windows                   # Cross-compile lambda for Windows (incremental)"
            echo "  $0 --platform=debug --force             # Force debug rebuild"
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
        
        # For root-level values, exclude platform sections
        # Get only lines before the "platforms" section
        local before_platforms=$(grep -n "\"platforms\"" "$file" | head -1 | cut -d: -f1)
        if [ -n "$before_platforms" ]; then
            # Search only in the root level (before platforms section)
            head -$((before_platforms - 1)) "$file" | grep "\"$key\"" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g'
        else
            # No platforms section, search the whole file
            grep "\"$key\"" "$file" | head -1 | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*"([^"]*).*/\1/' | sed -E 's/.*"'$key'"[[:space:]]*:[[:space:]]*([^,}]*).*/\1/' | sed 's/[",]//g'
        fi
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

# Function to collect source files from directories
collect_source_files_from_dirs() {
    local config_file="$1"
    local platform="$2"
    local collected_files=()
    
    # Get source_dirs array
    local source_dirs_list
    if command -v jq >/dev/null 2>&1; then
        if [ -n "$platform" ]; then
            # Try platform-specific source_dirs first, fallback to default
            source_dirs_list=$(jq -r ".platforms.$platform.source_dirs[]? // empty" "$config_file" 2>/dev/null)
            if [ -z "$source_dirs_list" ]; then
                source_dirs_list=$(jq -r ".source_dirs[]? // empty" "$config_file" 2>/dev/null)
            fi
        else
            source_dirs_list=$(jq -r ".source_dirs[]? // empty" "$config_file" 2>/dev/null)
        fi
    else
        # Fallback parsing without jq
        if [ -n "$platform" ]; then
            # Try platform-specific array first
            local platform_section=$(sed -n '/"platforms"/,/}/p' "$config_file" | sed -n '/"'$platform'"/,/}/p')
            if echo "$platform_section" | grep -q '"source_dirs"'; then
                source_dirs_list=$(echo "$platform_section" | sed -n '/"source_dirs":/,/]/p' | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "source_dirs")
            else
                source_dirs_list=$(get_json_array "source_dirs" "$config_file")
            fi
        else
            source_dirs_list=$(get_json_array "source_dirs" "$config_file")
        fi
    fi
    
    # Process each directory
    while IFS= read -r dir; do
        if [ -n "$dir" ] && [ -d "$dir" ]; then
            # Find all C and C++ source files in the directory (recursively)
            # Include common C/C++ extensions: .c, .cpp, .cc, .cxx, .c++
            while IFS= read -r -d '' file; do
                collected_files+=("$file")
            done < <(find "$dir" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o -name "*.c++" \) -print0 2>/dev/null)
        elif [ -n "$dir" ]; then
            echo "Warning: source_dirs entry '$dir' is not a valid directory" >&2
        fi
    done <<< "$source_dirs_list"
    
    # Output collected files (one per line)
    printf '%s\n' "${collected_files[@]}"
}

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
    if is_cpp_file "$file"; then
        echo "$CXX"
    else
        echo "$CC"
    fi
}

# Function to get appropriate warnings for a file type
get_warnings_for_file() {
    local file="$1"
    if is_cpp_file "$file"; then
        # Remove C-specific warnings that don't apply to C++
        echo "$WARNINGS" | sed 's/-Werror=incompatible-pointer-types//g' | sed 's/-Wincompatible-pointer-types//g'
    else
        echo "$WARNINGS"
    fi
}

# Function to get appropriate flags for a file type
get_flags_for_file() {
    local file="$1"
    if is_cpp_file "$file"; then
        local cpp_flags="$FLAGS -std=c++17"
        if [ "$CROSS_COMPILE" = "true" ]; then
            cpp_flags="$cpp_flags -fpermissive -Wno-fpermissive"
        fi
        echo "$cpp_flags"
    else
        echo "$FLAGS -std=c99"
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
    # Optimized: Use a more efficient approach
    if [ ${#INCLUDE_DIRS_ARRAY[@]} -gt 0 ]; then
        local newest_header=""
        # Use find with -newer for faster comparison (single pass)
        newest_header=$(find "${INCLUDE_DIRS_ARRAY[@]}" -name "*.h" -type f 2>/dev/null | \
                       xargs -r ls -t 2>/dev/null | head -1)
        
        if [ -n "$newest_header" ] && [ -f "$newest_header" ]; then
            NEWEST_HEADER_TIME=$(stat -f "%m" "$newest_header" 2>/dev/null || stat -c "%Y" "$newest_header" 2>/dev/null)
        fi
    fi
}



# Read configuration with platform override support
BUILD_DIR=$(get_json_value "build_dir" "$CONFIG_FILE" "$PLATFORM")
OUTPUT=$(get_json_value "output" "$CONFIG_FILE" "$PLATFORM")
DEBUG=$(get_json_value "debug" "$CONFIG_FILE" "$PLATFORM")
CROSS_COMPILE=$(get_json_value "cross_compile" "$CONFIG_FILE" "$PLATFORM")
TARGET_TRIPLET=$(get_json_value "target_triplet" "$CONFIG_FILE" "$PLATFORM")

# Default to native compilation if cross_compile is not specified
if [ -z "$CROSS_COMPILE" ]; then
    CROSS_COMPILE="false"
fi

# Auto-regenerate lambda-embed.h if lambda.h is newer or lambda-embed.h doesn't exist
LAMBDA_H_FILE="lambda/lambda.h"
LAMBDA_EMBED_H_FILE="lambda/lambda-embed.h"

if [ -f "$LAMBDA_H_FILE" ]; then
    # Check if lambda_embed.h needs to be regenerated
    NEED_REGENERATE=false
    
    if [ ! -f "$LAMBDA_EMBED_H_FILE" ]; then
        echo "lambda/lambda-embed.h not found, generating..."
        NEED_REGENERATE=true
    elif [ "$LAMBDA_H_FILE" -nt "$LAMBDA_EMBED_H_FILE" ]; then
        echo "lambda.h is newer than lambda/lambda-embed.h, regenerating..."
        NEED_REGENERATE=true
    fi
    
    if [ "$NEED_REGENERATE" = true ]; then
        if command -v xxd >/dev/null 2>&1; then
            echo "Regenerating $LAMBDA_EMBED_H_FILE from $LAMBDA_H_FILE..."
            xxd -i "$LAMBDA_H_FILE" > "$LAMBDA_EMBED_H_FILE"
            if [ $? -eq 0 ]; then
                echo "Successfully regenerated $LAMBDA_EMBED_H_FILE"
            else
                echo "Error: Failed to regenerate $LAMBDA_EMBED_H_FILE"
                exit 1
            fi
        else
            echo "Error: xxd command not found! Cannot regenerate $LAMBDA_EMBED_H_FILE"
            echo "Install xxd or manually run: xxd -i $LAMBDA_H_FILE > $LAMBDA_EMBED_H_FILE"
            exit 1
        fi
    fi
fi

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
    elif [ "$PLATFORM" = "debug" ]; then
        # Debug platform uses native compilation with debug flags
        CROSS_COMPILE="false"
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
    # Use build configuration or environment variables for compiler selection
    if [ -n "$CC" ]; then
        # Use environment variable if set
        CC="$CC"
    elif [ -f "build_lambda_config.json" ] && command -v jq >/dev/null 2>&1; then
        # Use compiler from build config
        CONFIG_COMPILER=$(jq -r '.compiler // "gcc"' build_lambda_config.json 2>/dev/null)
        CC="$CONFIG_COMPILER"
    else
        CC="gcc"  # Default to gcc for Linux compatibility
    fi
    
    if [ -n "$CXX" ]; then
        # Use environment variable if set
        CXX="$CXX"
    elif [ "$CC" = "gcc" ]; then
        CXX="g++"
    elif [ "$CC" = "clang" ]; then
        CXX="clang++"
    else
        CXX="${CC}++"
    fi
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
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS $lib"
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
                [ -n "$lib" ] && LINK_LIBS="$LINK_LIBS $lib"
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
    
    # Unicode support is always enabled - no conditional flags needed
    
    # Get all source files (unified approach with auto-detection)
    SOURCE_FILES_ARRAY=()
    
    # Read legacy source_files entries
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(jq -r '.source_files[]?' "$CONFIG_FILE" 2>/dev/null)
    
    # Collect additional source files from source_dirs
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(collect_source_files_from_dirs "$CONFIG_FILE" "$PLATFORM")
else
    # Fallback without jq - basic parsing
    if [ "$CROSS_COMPILE" = "true" ] || [ "$PLATFORM" = "windows" ]; then
        echo "Warning: jq not found, using fallback parsing for cross-compilation"
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -Iwindows-deps/include"
        LIBS="lambda/tree-sitter/libtree-sitter-windows.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a windows-deps/lib/libmir.a windows-deps/lib/liblexbor_static.a windows-deps/lib/libgmp.a"
        LINK_LIBS=""
        WARNINGS="-Wformat -Wincompatible-pointer-types -Wmultichar"
        FLAGS="-fms-extensions -static -DCROSS_COMPILE -D_WIN32"
        LINKER_FLAGS="-static-libgcc -static-libstdc++"
    else
        INCLUDES="-Ilambda/tree-sitter/lib/include -Ilambda/tree-sitter-lambda/bindings/c -I/usr/local/include -I/opt/homebrew/include -I."
        LIBS="lambda/tree-sitter/libtree-sitter.a lambda/tree-sitter-lambda/libtree-sitter-lambda.a /usr/local/lib/libmir.a /usr/local/lib/liblexbor_static.a"
        LINK_LIBS="-L/opt/homebrew/lib -lgmp"
        WARNINGS="-Werror=format -Werror=incompatible-pointer-types -Werror=multichar"
        FLAGS="-fms-extensions -pedantic -fcolor-diagnostics"
        LINKER_FLAGS=""
    fi
    
    # Parse all source files into unified array
    SOURCE_FILES_ARRAY=()
    
    # Read legacy source_files entries
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(get_json_array "source_files" "$CONFIG_FILE")
    
    # Collect additional source files from source_dirs
    while IFS= read -r line; do
        [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
    done < <(collect_source_files_from_dirs "$CONFIG_FILE" "$PLATFORM")
fi

# Add debug flag if enabled
if [ "$DEBUG" = "true" ]; then
    FLAGS="$FLAGS -g"
fi

# Add AddressSanitizer flags for debug builds (DISABLED for testing)
if [ "$DEBUG_BUILD" = true ]; then
    FLAGS="$FLAGS -DDEBUG -O0 -g3"
    # LINKER_FLAGS="$LINKER_FLAGS -fsanitize=address -fsanitize=undefined"
fi

# Initialize header file cache for faster incremental builds (fallback for files without .d files)
if [ "$FORCE_REBUILD" != true ]; then
    echo "Initializing dependency tracking..."
    init_header_cache
    if [ -n "$NEWEST_HEADER_TIME" ]; then
        echo "Dependency tracking: Using .d files (precise) + header cache (fallback)"
    else
        echo "Dependency tracking: Using .d files (precise) only"
    fi
fi

# Debug output
echo "Configuration loaded from: $CONFIG_FILE"
if [ -n "$PLATFORM" ]; then
    echo "Target platform: $PLATFORM"
fi
if [ "$DEBUG_BUILD" = true ]; then
    echo "Debug build: enabled (AddressSanitizer)"
fi
echo "Cross-compilation: $CROSS_COMPILE"
if [ "$FORCE_REBUILD" = true ]; then
    echo "Build mode: Full rebuild (--force)"
else
    echo "Build mode: Incremental"
fi
echo "Build directory: $BUILD_DIR"
echo "Output executable: $OUTPUT"

# Count C and C++ files automatically
C_FILES_COUNT=0
CPP_FILES_COUNT=0
for file in "${SOURCE_FILES_ARRAY[@]}"; do
    if is_cpp_file "$file"; then
        CPP_FILES_COUNT=$((CPP_FILES_COUNT + 1))
    else
        C_FILES_COUNT=$((C_FILES_COUNT + 1))
    fi
done

echo "Source files count: ${#SOURCE_FILES_ARRAY[@]} (${C_FILES_COUNT} C files, ${CPP_FILES_COUNT} C++ files)"

# Report source directories if any are configured
SOURCE_DIRS_COUNT=0
if command -v jq >/dev/null 2>&1; then
    if [ -n "$PLATFORM" ]; then
        SOURCE_DIRS_COUNT=$(jq -r ".platforms.$PLATFORM.source_dirs[]? // empty" "$CONFIG_FILE" 2>/dev/null | wc -l | tr -d ' ')
        if [ "$SOURCE_DIRS_COUNT" -eq 0 ]; then
            SOURCE_DIRS_COUNT=$(jq -r ".source_dirs[]? // empty" "$CONFIG_FILE" 2>/dev/null | wc -l | tr -d ' ')
        fi
    else
        SOURCE_DIRS_COUNT=$(jq -r ".source_dirs[]? // empty" "$CONFIG_FILE" 2>/dev/null | wc -l | tr -d ' ')
    fi
else
    SOURCE_DIRS_COUNT=$(get_json_array "source_dirs" "$CONFIG_FILE" "$PLATFORM" | wc -l | tr -d ' ')
fi

if [ "$SOURCE_DIRS_COUNT" -gt 0 ]; then
    echo "Sources=$4"
fi

# Function to compile a single file (for parallel execution)
compile_single_file() {
    local source="$1"
    local obj_file="$2"
    local compiler="$3"
    local warnings="$4"
    local flags="$5"
    
    echo "Compiling: $source -> $obj_file"
    
    # Capture compilation output and status
    local compile_output
    compile_output=$($compiler $INCLUDES -MMD -MP -c "$source" -o "$obj_file" $warnings $flags 2>&1)
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

# ===== UNIFIED BUILD SYSTEM: Compilation Phase =====
echo "🚀 Starting unified compilation process..."

# Capture compilation output for error reporting
output=$(build_compile_sources "$BUILD_DIR" "$INCLUDES" "$WARNINGS" "$FLAGS" "true" "$PARALLEL_JOBS" "${SOURCE_FILES_ARRAY[@]}" 2>&1)
compilation_exit_code=$?

# Extract object files from output (last line should be object files if successful)
if [ $compilation_exit_code -eq 0 ]; then
    OBJECT_FILES_LIST="$output"
else
    OBJECT_FILES_LIST=""
fi

# Check for compilation errors in the output (but don't display diagnostics yet)
format_compilation_diagnostics "$output" >/dev/null
error_count=$?

# Determine compilation success based on both exit code and error detection
if [ $compilation_exit_code -ne 0 ] || [ $error_count -gt 0 ]; then
    echo "❌ Compilation failed"
    compilation_success=false
    OBJECT_FILES=""
    linking_performed=false
else
    # Extract object files list from successful compilation
    OBJECT_FILES_LIST=$(build_compile_sources "$BUILD_DIR" "$INCLUDES" "$WARNINGS" "$FLAGS" "true" "$PARALLEL_JOBS" "${SOURCE_FILES_ARRAY[@]}" 2>/dev/null)
    OBJECT_FILES=$(echo "$OBJECT_FILES_LIST" | tr '\n' ' ' | sed 's/[[:space:]]*$//')
    echo "✅ Compilation completed successfully"
    compilation_success=true
    
    # ===== UNIFIED BUILD SYSTEM: Linking Phase =====
    echo "🔗 Starting unified linking process..."
    
    # Use the new unified linking function
    if build_link_objects "$OUTPUT" "$OBJECT_FILES" "" "$LIBS $LINK_LIBS" "$LINKER_FLAGS" "auto"; then
        echo "✅ Linking completed successfully"
        linking_performed=true
    else
        echo "❌ Linking failed"
        compilation_success=false
        linking_performed=false
    fi
fi

# Display compilation diagnostics at the end
format_compilation_diagnostics "$output"

# Display build status using centralized function
display_build_status "$compilation_success" "$linking_performed" "$OUTPUT" "$BUILD_DIR" "$CROSS_COMPILE"
exit_code=$?

exit $exit_code
fi