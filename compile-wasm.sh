#!/bin/bash

# WASM Compilation Script for Lambda Project
# Compiles Lambda C files into WASM using WASI SDK and JSON configuration
# Usage: ./compile-wasm.sh [config_file] [--debug] [--force] [--jobs=N] [--help]

set -e  # Exit on any error

# Default configuration
DEFAULT_CONFIG="build_lambda_wasm_config.json"
CONFIG_FILE="$DEFAULT_CONFIG"
WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
FORCE_REBUILD=false
PARALLEL_JOBS=""
DEBUG_BUILD=false
LINKING_ONLY=false

# Colors for output
RED="\033[0;31m"
YELLOW="\033[1;33m"
GREEN="\033[0;32m"
BLUE="\033[0;34m"
RESET="\033[0m"

# Function to show help
show_help() {
    cat << EOF
WASM Compilation Script for Lambda Project

Usage: $0 [config_file] [options]

Arguments:
    config_file          JSON configuration file (default: $DEFAULT_CONFIG)

Options:
    --debug, -d          Build debug version with debug symbols
    --force, -f          Force rebuild all files (disable incremental compilation)
    --jobs=N, -j N       Number of parallel compilation jobs (default: auto-detect)
    --linking-only, -l   Only perform linking step (requires existing object files)
    --clean-deps         Clean dependency files (.d files) and exit
    --help, -h           Show this help information

Environment Variables:
    WASI_SDK_PATH        Path to WASI SDK (default: /opt/wasi-sdk)

Examples:
    $0                              # Basic build with default config
    $0 --debug                      # Debug build
    $0 --force                      # Force full rebuild
    $0 --jobs=4                     # Use 4 parallel jobs
    $0 --linking-only               # Link existing object files only
    $0 custom_config.json           # Use custom configuration
EOF
}

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
        --linking-only|-l)
            LINKING_ONLY=true
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
        --help|-h)
            show_help
            exit 0
            ;;
        -*)
            echo -e "${RED}Error: Unknown option $1${RESET}"
            show_help
            exit 1
            ;;
        *)
            # Positional argument - config file
            CONFIG_FILE="$1"
            shift
            ;;
    esac
done

echo -e "${GREEN}Lambda WASM Build Script${RESET}"
echo "Configuration: $CONFIG_FILE"
echo "WASI SDK: $WASI_SDK_PATH"

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}Error: Configuration file '$CONFIG_FILE' not found${RESET}"
    exit 1
fi

# Check if jq is available for JSON parsing
if ! command -v jq >/dev/null 2>&1; then
    echo -e "${YELLOW}Warning: jq not found. Using basic JSON parsing.${RESET}"
    HAS_JQ=false
else
    HAS_JQ=true
fi

# Function to parse JSON config
parse_config() {
    local key="$1"
    local default="$2"
    
    if [ "$HAS_JQ" = true ]; then
        local value=$(jq -r ".$key // empty" "$CONFIG_FILE" 2>/dev/null)
        if [ -z "$value" ] || [ "$value" = "null" ]; then
            echo "$default"
        else
            echo "$value"
        fi
    else
        # Basic fallback parsing
        case "$key" in
            "output")
                grep -o '"output"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | sed 's/.*"output"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo "$default"
                ;;
            "build_dir")
                grep -o '"build_dir"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | sed 's/.*"build_dir"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo "$default"
                ;;
            "target_triplet")
                grep -o '"target_triplet"[[:space:]]*:[[:space:]]*"[^"]*"' "$CONFIG_FILE" | sed 's/.*"target_triplet"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/' || echo "$default"
                ;;
            *)
                echo "$default"
                ;;
        esac
    fi
}

# Function to parse JSON array
parse_array() {
    local key="$1"
    
    if [ "$HAS_JQ" = true ]; then
        jq -r ".$key[]? // empty" "$CONFIG_FILE" 2>/dev/null
    else
        # Basic fallback for arrays - extract strings from the array
        grep -A 50 "\"$key\"" "$CONFIG_FILE" | grep -o '"[^"]*"' | sed 's/"//g' | grep -v "^$key$" | head -50
    fi
}

# Parse configuration
OUTPUT=$(parse_config "output" "lambda.wasm")
BUILD_DIR=$(parse_config "build_dir" "build_wasm")
TARGET=$(parse_config "target_triplet" "wasm32-wasi")

# Override debug setting if command line flag is set
if [ "$DEBUG_BUILD" = true ]; then
    DEBUG_FLAG=true
else
    DEBUG_FLAG=$(parse_config "debug" "false")
fi

echo "Output: $OUTPUT"
echo "Build directory: $BUILD_DIR"
echo "Target: $TARGET"

# Check WASI SDK
CC="$WASI_SDK_PATH/bin/clang"
if [ ! -x "$CC" ]; then
    echo -e "${RED}Error: WASI clang compiler not found at $CC${RESET}"
    echo "Please install WASI SDK or set WASI_SDK_PATH environment variable"
    echo "Download from: https://github.com/WebAssembly/wasi-sdk/releases"
    exit 1
fi

echo "Compiler: $CC"

# Create build directory
mkdir -p "$BUILD_DIR"

# If linking-only mode, check for existing object files
if [ "$LINKING_ONLY" = true ]; then
    if [ ! -d "$BUILD_DIR" ] || [ -z "$(ls -A $BUILD_DIR/*.o 2>/dev/null)" ]; then
        echo -e "${RED}Error: No object files found in $BUILD_DIR${RESET}"
        echo "Please run full compilation first without --linking-only flag"
        exit 1
    fi
    
    OBJ_COUNT=$(ls -1 $BUILD_DIR/*.o 2>/dev/null | wc -l)
    echo "Found $OBJ_COUNT object files in $BUILD_DIR"
    echo -e "${YELLOW}Linking-only mode: Skipping compilation${RESET}"
    
    # Jump to linking step
    SOURCES=""
else
    # Collect source files
    echo -e "${BLUE}Collecting source files...${RESET}"
    SOURCES=""
    
    # Add source files from config
    while IFS= read -r file; do
        if [ -n "$file" ] && [ -f "$file" ]; then
            SOURCES="$SOURCES $file"
        elif [ -n "$file" ]; then
            echo -e "${YELLOW}Warning: Source file '$file' not found${RESET}"
        fi
    done < <(parse_array "source_files")
    
    # Add source directories from config
    while IFS= read -r dir; do
        if [ -n "$dir" ] && [ -d "$dir" ]; then
            for file in "$dir"/*.c; do
                if [ -f "$file" ]; then
                    SOURCES="$SOURCES $file"
                fi
            done
        elif [ -n "$dir" ]; then
            echo -e "${YELLOW}Warning: Source directory '$dir' not found${RESET}"
        fi
    done < <(parse_array "source_dirs")
    
    if [ -z "$SOURCES" ]; then
        echo -e "${RED}Error: No source files found${RESET}"
        exit 1
    fi
    
    echo "Found $(echo $SOURCES | wc -w) source files"
fi

# Build compiler flags
CFLAGS="--target=$TARGET"

# Add debug flags
if [ "$DEBUG_FLAG" = "true" ]; then
    CFLAGS="$CFLAGS -g -O1"
    echo "Debug build enabled"
else
    CFLAGS="$CFLAGS -O2"
fi

# Add flags from config
while IFS= read -r flag; do
    if [ -n "$flag" ]; then
        CFLAGS="$CFLAGS -$flag"
    fi
done < <(parse_array "flags")

# Add include directories from libraries
if [ "$HAS_JQ" = true ]; then
    while IFS= read -r include; do
        if [ -n "$include" ] && [ -d "$include" ]; then
            CFLAGS="$CFLAGS -I$include"
        fi
    done < <(jq -r '.libraries[]?.include // empty' "$CONFIG_FILE" 2>/dev/null)
else
    # Manual include parsing for fallback
    CFLAGS="$CFLAGS -I. -Iinclude -Ilib -Ilambda -Iwasm-deps/include"
fi

# Add warning flags
while IFS= read -r warning; do
    if [ -n "$warning" ]; then
        CFLAGS="$CFLAGS -W$warning"
    fi
done < <(parse_array "warnings")

echo "Compiler flags: $CFLAGS"

# Compilation phase (unless linking-only)
if [ "$LINKING_ONLY" != true ]; then
    # Determine parallel jobs
    if [ -z "$PARALLEL_JOBS" ]; then
        if command -v nproc >/dev/null 2>&1; then
            PARALLEL_JOBS=$(nproc)
        elif command -v sysctl >/dev/null 2>&1; then
            PARALLEL_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo "4")
        else
            PARALLEL_JOBS=4
        fi
    fi
    
    echo "Using $PARALLEL_JOBS parallel jobs"
    echo -e "${BLUE}Compiling source files...${RESET}"
    
    # Compile sources
    COMPILE_ERRORS=0
    COMPILED_COUNT=0
    SKIPPED_COUNT=0
    
    for source in $SOURCES; do
        obj="$BUILD_DIR/$(basename "$source" .c).o"
        
        # Check if recompilation is needed
        if [ "$FORCE_REBUILD" = false ] && [ -f "$obj" ] && [ "$obj" -nt "$source" ]; then
            ((SKIPPED_COUNT++))
            continue
        fi
        
        echo "Compiling $source..."
        if ! $CC $CFLAGS -c "$source" -o "$obj" 2>&1; then
            echo -e "${RED}Error compiling $source${RESET}"
            ((COMPILE_ERRORS++))
        else
            ((COMPILED_COUNT++))
        fi
    done
    
    echo "Compilation completed: $COMPILED_COUNT compiled, $SKIPPED_COUNT skipped"
    
    if [ $COMPILE_ERRORS -gt 0 ]; then
        echo -e "${RED}Compilation failed with $COMPILE_ERRORS errors${RESET}"
        exit 1
    fi
fi

# Linking phase
echo -e "${BLUE}Linking WASM module...${RESET}"

# Build linker flags
LINKER_FLAGS=""
while IFS= read -r flag; do
    if [ -n "$flag" ]; then
        LINKER_FLAGS="$LINKER_FLAGS -$flag"
    fi
done < <(parse_array "linker_flags")

# Link command
LINK_CMD="$CC --target=$TARGET -O2 -DWASM_BUILD $BUILD_DIR/*.o $LINKER_FLAGS -o $OUTPUT"
echo "Link command: $LINK_CMD"

if $LINK_CMD; then
    echo -e "${GREEN}WASM build successful: $OUTPUT${RESET}"
    echo "File size: $(ls -lh "$OUTPUT" | awk '{print $5}')"
    
    # Test the WASM file if Node.js is available
    if command -v node >/dev/null 2>&1 && [ -f "test/lambda-wasm-node.js" ]; then
        echo -e "${BLUE}Testing WASM module...${RESET}"
        cp "$OUTPUT" test/
        if node test/lambda-wasm-node.js >/dev/null 2>&1; then
            echo -e "${GREEN}WASM module test passed!${RESET}"
        else
            echo -e "${YELLOW}WASM module test failed, but build succeeded${RESET}"
        fi
    fi
    
    echo -e "${GREEN}Build completed successfully!${RESET}"
    echo
    echo "Usage:"
    echo "  - Copy $OUTPUT to your web project"
    echo "  - Use test/lambda-wasm-node.js as a reference for integration"
    echo "  - Available exports: wasm_lambda_version, wasm_lambda_init, wasm_lambda_process_string,"
    echo "    wasm_lambda_runtime_new, wasm_lambda_runtime_free, wasm_lambda_run_code, wasm_lambda_item_to_string"
else
    echo -e "${RED}WASM linking failed${RESET}"
    exit 1
fi
