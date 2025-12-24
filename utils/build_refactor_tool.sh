#!/bin/bash
# Build script for the clang-based refactoring tool

set -e

cd "$(dirname "$0")"

echo "Building refactor_to_log_debug using libclang..."

# Detect platform and set compiler flags
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    CLANG_INCLUDE="/opt/homebrew/opt/llvm/include"
    CLANG_LIB="/opt/homebrew/opt/llvm/lib"
    
    # Fallback to system paths if homebrew llvm not found
    if [ ! -d "$CLANG_INCLUDE" ]; then
        CLANG_INCLUDE="/usr/local/opt/llvm/include"
        CLANG_LIB="/usr/local/opt/llvm/lib"
    fi
    
    clang++ -std=c++17 \
        -I"$CLANG_INCLUDE" \
        -L"$CLANG_LIB" \
        -lclang \
        -Wl,-rpath,"$CLANG_LIB" \
        refactor_to_log_debug.cpp \
        -o refactor_to_log_debug
else
    # Linux/Other
    clang++ -std=c++17 \
        -lclang \
        refactor_to_log_debug.cpp \
        -o refactor_to_log_debug
fi

if [ $? -eq 0 ]; then
    echo "✓ Build successful!"
    echo "Usage: ./refactor_to_log_debug <source_file> [--dry-run] [--backup]"
    echo "Or use: make tidy-printf FILE='pattern' [DRY_RUN=1] [BACKUP=1]"
else
    echo "✗ Build failed"
    exit 1
fi
