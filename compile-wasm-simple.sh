#!/bin/bash

# Simple WASM Compilation Script for Lambda Project
# This script uses the existing successful build approach

set -e  # Exit on any error

# Configuration
WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
CC="$WASI_SDK_PATH/bin/clang"
BUILD_DIR="build_wasm"
OUTPUT="lambda.wasm"

# Colors for output
GREEN="\033[0;32m"
BLUE="\033[0;34m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
RESET="\033[0m"

echo -e "${GREEN}Lambda WASM Build Script (Updated)${RESET}"
echo "Using WASI SDK: $WASI_SDK_PATH"
echo "Using C compiler: $CC"

# Check if WASI SDK is available
if [ ! -x "$CC" ]; then
    echo -e "${RED}Error: WASI clang compiler not found at $CC${RESET}"
    echo "Please install WASI SDK or set WASI_SDK_PATH environment variable"
    exit 1
fi

# Check if build directory exists with object files
if [ ! -d "$BUILD_DIR" ] || [ -z "$(ls -A $BUILD_DIR/*.o 2>/dev/null)" ]; then
    echo -e "${RED}Error: No object files found in $BUILD_DIR${RESET}"
    echo "Please run the full compilation first with: ./compile-wasm.sh"
    exit 1
fi

# Count existing object files
OBJ_COUNT=$(ls -1 $BUILD_DIR/*.o 2>/dev/null | wc -l)
echo "Found $OBJ_COUNT object files in $BUILD_DIR"

# WASM-specific linker flags with specific exports (working configuration)
LINKER_FLAGS="-L$WASI_SDK_PATH/share/wasi-sysroot/lib/wasm32-wasi"
LINKER_FLAGS="$LINKER_FLAGS -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-getpid -lwasi-emulated-mman"
EXPORTS="-Wl,--export=wasm_lambda_version,--export=wasm_lambda_init,--export=wasm_lambda_process_string"
EXPORTS="$EXPORTS,--export=wasm_lambda_runtime_new,--export=wasm_lambda_runtime_free,--export=wasm_lambda_run_code"
EXPORTS="$EXPORTS,--export=wasm_lambda_item_to_string,--allow-undefined"

# Link all object files to create WASM module
echo -e "${BLUE}Linking WASM module...${RESET}"
echo "Command: $CC --target=wasm32-wasi -O2 -DWASM_BUILD $BUILD_DIR/*.o $LINKER_FLAGS $EXPORTS -o $OUTPUT"

$CC --target=wasm32-wasi -O2 -DWASM_BUILD $BUILD_DIR/*.o $LINKER_FLAGS $EXPORTS -o "$OUTPUT"

# Check result
if [ -f "$OUTPUT" ]; then
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
    echo -e "${RED}WASM build failed${RESET}"
    exit 1
fi
