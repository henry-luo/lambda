#!/bin/bash

# Configuration file
CONFIG_FILE="${1:-build_lambda_config.json}"

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
        # Fallback parsing for arrays - extract content between quotes in the array
        sed -n '/"'$key'":/,/]/p' "$file" | grep '"' | sed 's/.*"\([^"]*\)".*/\1/' | grep -v '^[[:space:]]*$' | grep -v "$key"
    fi
}

# Read configuration
BUILD_DIR=$(get_json_value "build_dir" "$CONFIG_FILE")
OUTPUT=$(get_json_value "output" "$CONFIG_FILE")
DEBUG=$(get_json_value "debug" "$CONFIG_FILE")

# Validate required fields
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="build"  # Default fallback
fi

if [ -z "$OUTPUT" ]; then
    OUTPUT="lambda.exe"  # Default fallback
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Build compiler flags
INCLUDES=""
LIBS=""
LINK_LIBS=""
WARNINGS=""
FLAGS=""

# Process libraries
if command -v jq >/dev/null 2>&1; then
    # Extract library information using jq
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
    
    # Process warnings
    while IFS= read -r warning; do
        [ -n "$warning" ] && WARNINGS="$WARNINGS -Werror=$warning"
    done < <(jq -r '.warnings[]?' "$CONFIG_FILE")
    
    # Process flags
    while IFS= read -r flag; do
        [ -n "$flag" ] && FLAGS="$FLAGS -$flag"
    done < <(jq -r '.flags[]?' "$CONFIG_FILE")
    
    # Process linker flags
    LINKER_FLAGS=""
    while IFS= read -r flag; do
        [ -n "$flag" ] && LINKER_FLAGS="$LINKER_FLAGS -$flag"
    done < <(jq -r '.linker_flags[]?' "$CONFIG_FILE")
    
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
    INCLUDES="-Ilambda/tree-sitter/lib/include -I/usr/local/include -I/opt/homebrew/include"
    LIBS="lambda/tree-sitter/libtree-sitter.a /usr/local/lib/libmir.a /usr/local/lib/libzlog.a"
    LINK_LIBS="-L/opt/homebrew/lib -lgmp"
    WARNINGS="-Werror=format -Werror=incompatible-pointer-types -Werror=multichar"
    FLAGS="-fms-extensions -pedantic -fcolor-diagnostics"
    LINKER_FLAGS=""
    
    # Parse linker flags
    while IFS= read -r flag; do
        [ -n "$flag" ] && LINKER_FLAGS="$LINKER_FLAGS -$flag"
    done < <(get_json_array "linker_flags" "$CONFIG_FILE")
    
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

# Debug output
echo "Configuration loaded from: $CONFIG_FILE"
echo "Raw BUILD_DIR: '$BUILD_DIR'"
echo "Raw OUTPUT: '$OUTPUT'"
echo "Raw DEBUG: '$DEBUG'"
echo "Build directory: $BUILD_DIR"
echo "Output executable: $OUTPUT"
echo "Source files count: ${#SOURCE_FILES_ARRAY[@]}"
echo "C++ files count: ${#CPP_FILES_ARRAY[@]}"

# Debug: Show first few source files
if [ ${#SOURCE_FILES_ARRAY[@]} -gt 0 ]; then
    echo "First source file: '${SOURCE_FILES_ARRAY[0]}'"
fi
echo

# Compile each C source file individually
output=""
OBJECT_FILES=""
compilation_success=true

for source in "${SOURCE_FILES_ARRAY[@]}"; do
    if [ -f "$source" ]; then
        # Generate object file name from source file
        obj_name=$(basename "$source" | sed 's/\.[^.]*$//')
        obj_file="$BUILD_DIR/${obj_name}.o"
        OBJECT_FILES="$OBJECT_FILES $obj_file"
        
        echo "Compiling: $source -> $obj_file"
        
        # Compile individual source file (only use includes and warnings/flags, not linker flags)
        compile_output=$(clang $INCLUDES \
          -c "$source" -o "$obj_file" \
          $WARNINGS $FLAGS 2>&1)
        
        # Capture output and check for errors
        if [ $? -ne 0 ]; then
            compilation_success=false
        fi
        
        # Append to total output
        if [ -n "$compile_output" ]; then
            output="$output\n$compile_output"
        fi
    else
        echo "Warning: Source file '$source' not found"
        compilation_success=false
    fi
done

# Compile C++ files if any
if [ ${#CPP_FILES_ARRAY[@]} -gt 0 ]; then
    for cpp_file in "${CPP_FILES_ARRAY[@]}"; do
        if [ -f "$cpp_file" ]; then
            # Generate object file name from cpp file
            cpp_obj_name=$(basename "$cpp_file" | sed 's/\.[^.]*$//')
            cpp_obj_file="$BUILD_DIR/${cpp_obj_name}.o"
            OBJECT_FILES="$OBJECT_FILES $cpp_obj_file"
            
            echo "Compiling C++: $cpp_file -> $cpp_obj_file"
            cpp_output=$(clang++ $INCLUDES -c "$cpp_file" -o "$cpp_obj_file" $WARNINGS $FLAGS 2>&1)
            if [ $? -ne 0 ]; then
                compilation_success=false
            fi
            if [ -n "$cpp_output" ]; then
                output="$output\n$cpp_output"
            fi
        else
            echo "Warning: C++ file '$cpp_file' not found"
            compilation_success=false
        fi
    done
fi

# Link final executable only if compilation was successful
if [ "$compilation_success" = true ]; then
    echo "Linking: $OUTPUT"
    link_output=$(clang++ $OBJECT_FILES $LIBS $LINK_LIBS $LINKER_FLAGS -o "$OUTPUT" 2>&1)
    if [ $? -ne 0 ]; then
        compilation_success=false
    fi
    if [ -n "$link_output" ]; then
        output="$output\n$link_output"
    fi
fi

# Output the captured, colorized messages
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

# Final build status
if [ "$compilation_success" = true ]; then
    echo -e "${GREEN}Build successful: $OUTPUT${RESET}"
    exit 0
else
    echo -e "${RED}Build failed${RESET}"
    exit 1
fi


