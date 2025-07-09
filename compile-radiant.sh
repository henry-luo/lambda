#!/bin/bash

# Enhanced Radiant compilation script
# Usage: ./compile-radiant.sh [config_file]

# Configuration file default
CONFIG_FILE="build_radiant_config.json"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --help|-h)
            echo "Usage: $0 [config_file]"
            echo ""
            echo "Arguments:"
            echo "  config_file           JSON configuration file (default: build_radiant_config.json)"
            echo ""
            echo "Examples:"
            echo "  $0                    # Use default config"
            echo "  $0 custom.json        # Use custom config"
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

# Check if jq is installed
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required to parse JSON. Please install jq."
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Configuration file '$CONFIG_FILE' not found!"
    exit 1
fi

# Set compiler
CC="clang"

echo "Using C compiler: $CC"
echo "Configuration loaded from: $CONFIG_FILE"

# Get output name from JSON
OUTPUT=$(jq -r '.output' "$CONFIG_FILE")
if [ "$OUTPUT" = "null" ] || [ -z "$OUTPUT" ]; then
    echo "Error: Output field is missing or empty in config"
    exit 1
fi

echo "Output executable: $OUTPUT"

# Build compiler flags
INCLUDES=""
LIBS=""
WARNINGS=""
FLAGS=""

# Get source files into array
SOURCE_FILES_ARRAY=()
while IFS= read -r line; do
    [ -n "$line" ] && SOURCE_FILES_ARRAY+=("$line")
done < <(jq -r '.source_files[]?' "$CONFIG_FILE" 2>/dev/null)

echo "Source files count: ${#SOURCE_FILES_ARRAY[@]}"
echo

# Process libraries
LIB_COUNT=$(jq -r '.libraries | length' "$CONFIG_FILE")
for ((i=0; i<LIB_COUNT; i++)); do
    NAME=$(jq -r ".libraries[$i].name" "$CONFIG_FILE")
    INCLUDE=$(jq -r ".libraries[$i].include" "$CONFIG_FILE")
    LIB=$(jq -r ".libraries[$i].lib" "$CONFIG_FILE")
    LINK=$(jq -r ".libraries[$i].link" "$CONFIG_FILE")
    
    [ "$INCLUDE" != "null" ] && [ -n "$INCLUDE" ] && INCLUDES="$INCLUDES -I$INCLUDE"
    
    if [ "$LINK" = "static" ]; then
        [ "$LIB" != "null" ] && [ -n "$LIB" ] && LIBS="$LIBS $LIB"
    else
        [ "$LIB" != "null" ] && [ -n "$LIB" ] && LIBS="$LIBS -L$LIB -l$NAME"
    fi
done

# Process warnings
WARNINGS_JSON=$(jq -r '.warnings[]?' "$CONFIG_FILE" 2>/dev/null)
while IFS= read -r warning; do
    [ -n "$warning" ] && WARNINGS="$WARNINGS -Werror=$warning"
done <<< "$WARNINGS_JSON"

# Process flags
FLAGS_JSON=$(jq -r '.flags[]?' "$CONFIG_FILE" 2>/dev/null)
while IFS= read -r flag; do
    [ -n "$flag" ] && FLAGS="$FLAGS -$flag"
done <<< "$FLAGS_JSON"

# Initialize compilation tracking
output=""
compilation_success=true

echo "Starting compilation..."
echo

# Build final compile command
COMPILE_CMD="$CC $INCLUDES ${SOURCE_FILES_ARRAY[*]} $LIBS $WARNINGS $FLAGS -o $OUTPUT"

echo "Compile command:"
echo "$COMPILE_CMD"
echo

# Execute compilation and capture output
compile_output=$($COMPILE_CMD 2>&1)
compile_exit_code=$?

# Check if compilation succeeded
if [ $compile_exit_code -ne 0 ]; then
    compilation_success=false
fi

# Store output for summary
output="$compile_output"

# Output the captured messages
if [ -n "$output" ]; then
    echo "$output"
fi

# Count errors and warnings (ignores color codes)
num_errors=$(echo "$output" | grep -c "error:" || echo "0")
num_warnings=$(echo "$output" | grep -c "warning:" || echo "0")

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
    
    # Show file info
    if command -v file >/dev/null 2>&1; then
        echo "File type information:"
        file "$OUTPUT" 2>/dev/null
    fi
    
    # Show file size
    if [ -f "$OUTPUT" ]; then
        file_size=$(ls -lh "$OUTPUT" | awk '{print $5}')
        echo "File size: $file_size"
    fi
    
    exit 0
else
    echo -e "${RED}Build failed${RESET}"
    exit 1
fi
