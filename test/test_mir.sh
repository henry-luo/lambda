#!/bin/bash

# Test script for MIR JIT functionality
# This script compiles and runs the MIR tests using Criterion

echo "Building and running MIR JIT tests..."

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required to parse the configuration file"
    echo "Please install jq: brew install jq"
    exit 1
fi

# Get the script directory and project root
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_FILE="$PROJECT_ROOT/build_lambda_config.json"

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file $CONFIG_FILE not found!"
    exit 1
fi

echo "Loading object files from $CONFIG_FILE..."

# Extract source files from JSON and convert to object file paths
OBJECT_FILES=()
while IFS= read -r source_file; do
    # Convert source file path to object file path
    # Remove leading directory path and change extension to .o
    obj_file="build/$(basename "$source_file" .c).o"
    if [ -f "$PROJECT_ROOT/$obj_file" ]; then
        OBJECT_FILES+=("$PROJECT_ROOT/$obj_file")
    else
        echo "Warning: Object file $obj_file not found. You may need to build the project first."
    fi
done < <(jq -r '.source_files[]' "$CONFIG_FILE" | grep -E '\.(c|cpp)$')

echo "Found ${#OBJECT_FILES[@]} object files"

# Check if we have any object files
if [ ${#OBJECT_FILES[@]} -eq 0 ]; then
    echo "Error: No object files found. Please build the project first using:"
    echo "  cd $PROJECT_ROOT && ./compile.sh"
    exit 1
fi

# Extract compiler flags from JSON
COMPILER_FLAGS=$(jq -r '.compiler_flags // []' "$CONFIG_FILE")
LIBRARIES=$(jq -r '.libraries // []' "$CONFIG_FILE")

# Default compiler flags for MIR testing
DEFAULT_FLAGS="-std=c99 -Wall -Wextra -O2 -g -fms-extensions -pedantic"

# Build include directory flags from libraries
INCLUDE_FLAGS=""
STATIC_LIBS=""
DYNAMIC_LIBS=""

if [ "$LIBRARIES" != "null" ] && [ "$LIBRARIES" != "[]" ]; then
    while IFS= read -r lib_info; do
        name=$(echo "$lib_info" | jq -r '.name')
        include=$(echo "$lib_info" | jq -r '.include // empty')
        lib=$(echo "$lib_info" | jq -r '.lib // empty')
        link=$(echo "$lib_info" | jq -r '.link // "static"')
        
        # Add include directory if specified
        if [ -n "$include" ] && [ "$include" != "null" ]; then
            INCLUDE_FLAGS="$INCLUDE_FLAGS -I$include"
        fi
        
        # Add library based on link type
        if [ "$link" = "static" ]; then
            if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                STATIC_LIBS="$STATIC_LIBS $lib"
            fi
        else
            if [ -n "$lib" ] && [ "$lib" != "null" ]; then
                DYNAMIC_LIBS="$DYNAMIC_LIBS -L$lib -l$name"
            fi
        fi
    done < <(echo "$LIBRARIES" | jq -c '.[]')
fi

# Additional flags for testing
TEST_FLAGS="-lcriterion"

# Check if criterion is available
if ! pkg-config --exists criterion; then
    echo "Warning: Criterion testing framework not found via pkg-config"
    echo "Trying to compile without pkg-config..."
else
    echo "Found Criterion testing framework"
    TEST_FLAGS="$TEST_FLAGS $(pkg-config --cflags --libs criterion)"
fi

# Compile the test
echo "Compiling MIR test..."
echo "Compiler command:"
echo "gcc $DEFAULT_FLAGS $INCLUDE_FLAGS -o test_mir.exe test_mir.c ${OBJECT_FILES[*]} $STATIC_LIBS $DYNAMIC_LIBS $TEST_FLAGS"

cd "$PROJECT_ROOT"
gcc $DEFAULT_FLAGS $INCLUDE_FLAGS -o test/test_mir.exe test/test_mir.c "${OBJECT_FILES[@]}" $STATIC_LIBS $DYNAMIC_LIBS $TEST_FLAGS

if [ $? -ne 0 ]; then
    echo "Error: Compilation failed!"
    exit 1
fi

echo "Compilation successful!"

# Run the test
echo "Running MIR tests..."
echo "=========================================="
if [ -f "test/test_mir.exe" ]; then
    cd test
    ./test_mir.exe
    TEST_RESULT=$?
    cd ..
    
    echo "=========================================="
    if [ $TEST_RESULT -eq 0 ]; then
        echo "All MIR tests passed!"
    else
        echo "Some MIR tests failed (exit code: $TEST_RESULT)"
    fi
    
    # Clean up
    echo "Cleaning up..."
    rm -f test/test_mir.exe
    
    exit $TEST_RESULT
else
    echo "Error: Test executable not found!"
    exit 1
fi
