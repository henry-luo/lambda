#!/bin/bash

# Test script for lambda functionality
# This script compiles and runs the lambda tests using Criterion

echo "Building and running lambda tests..."

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required to parse the configuration file"
    echo "Please install jq: brew install jq"
    exit 1
fi

# Load object files from build_lambda_config.json
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
    # Remove the directory structure and change extension to .o
    obj_file="$PROJECT_ROOT/build/$(basename "$source_file" .c).o"
    OBJECT_FILES+=("$obj_file")
done < <(jq -r '.source_files[]' "$CONFIG_FILE")

echo "Found ${#OBJECT_FILES[@]} object files to link"

# Change to test directory
cd "$SCRIPT_DIR"

# Extract include directories from config
INCLUDE_DIRS=()
INCLUDE_DIRS+=("-I$PROJECT_ROOT/lib/mem-pool/include")
INCLUDE_DIRS+=("-I$PROJECT_ROOT/lambda")
INCLUDE_DIRS+=("-I$PROJECT_ROOT/lib")

# Add library include directories from config
while IFS= read -r include_dir; do
    if [[ "$include_dir" != "null" && -n "$include_dir" ]]; then
        # Handle relative paths by making them relative to project root
        if [[ "$include_dir" = /* ]]; then
            # Absolute path
            INCLUDE_DIRS+=("-I$include_dir")
        else
            # Relative path - make it relative to project root
            INCLUDE_DIRS+=("-I$PROJECT_ROOT/$include_dir")
        fi
    fi
done < <(jq -r '.libraries[].include' "$CONFIG_FILE")

# Extract library directories and static libraries
LIB_DIRS=()
STATIC_LIBS=()
DYNAMIC_LIBS=()

while read -r lib_info; do
    name=$(echo "$lib_info" | jq -r '.name')
    lib_path=$(echo "$lib_info" | jq -r '.lib')
    link_type=$(echo "$lib_info" | jq -r '.link')
    
    if [[ "$link_type" == "static" ]]; then
        # Handle relative paths for static libraries
        if [[ "$lib_path" = /* ]]; then
            # Absolute path
            STATIC_LIBS+=("$lib_path")
        else
            # Relative path - make it relative to project root
            STATIC_LIBS+=("$PROJECT_ROOT/$lib_path")
        fi
    elif [[ "$link_type" == "dynamic" ]]; then
        # Handle relative paths for library directories
        if [[ "$lib_path" = /* ]]; then
            # Absolute path
            LIB_DIRS+=("-L$lib_path")
        else
            # Relative path - make it relative to project root
            LIB_DIRS+=("-L$PROJECT_ROOT/$lib_path")
        fi
        DYNAMIC_LIBS+=("-l$name")
    fi
done < <(jq -c '.libraries[]' "$CONFIG_FILE")

# Add additional library directories for criterion
LIB_DIRS+=("-L/opt/homebrew/lib")
LIB_DIRS+=("-L/opt/homebrew/Cellar/criterion/2.4.2_2/lib")
INCLUDE_DIRS+=("-I/opt/homebrew/include")
INCLUDE_DIRS+=("-I/opt/homebrew/Cellar/criterion/2.4.2_2/include")

# Debug: Show what we're going to link
echo "Debug: Object files to link:"
printf '%s\n' "${OBJECT_FILES[@]}"
echo ""
echo "Debug: Static libraries to link:"
printf '%s\n' "${STATIC_LIBS[@]}"
echo ""

# Check if object files exist
missing_files=()
for obj_file in "${OBJECT_FILES[@]}"; do
    if [ ! -f "$obj_file" ]; then
        missing_files+=("$obj_file")
    fi
done

if [ ${#missing_files[@]} -gt 0 ]; then
    echo "Warning: The following object files are missing:"
    printf '%s\n' "${missing_files[@]}"
    echo "You may need to run the build first to generate these files."
fi

# Check if static libraries exist
missing_libs=()
for lib_file in "${STATIC_LIBS[@]}"; do
    if [ ! -f "$lib_file" ]; then
        missing_libs+=("$lib_file")
    fi
done

if [ ${#missing_libs[@]} -gt 0 ]; then
    echo "Warning: The following static libraries are missing:"
    printf '%s\n' "${missing_libs[@]}"
fi

# Compile the test
echo "Compiling test_lambda..."
clang -o test_lambda.exe test_lambda.c "${OBJECT_FILES[@]}" \
    "${INCLUDE_DIRS[@]}" \
    "${LIB_DIRS[@]}" \
    "${STATIC_LIBS[@]}" \
    "${DYNAMIC_LIBS[@]}" \
    -lcriterion \
    -fms-extensions

if [ $? -eq 0 ]; then
    echo "Build successful! Running tests..."
    echo "=================================="
    ./test_lambda.exe --verbose
else
    echo "Build failed!"
    exit 1
fi
