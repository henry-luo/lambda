#!/bin/bash

# Check if jq is installed
if ! command -v jq &> /dev/null; then
    echo "Error: jq is required to parse JSON. Please install jq."
    exit 1
fi

# Check if config file exists
CONFIG_FILE="build_config.json"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Config file $CONFIG_FILE not found"
    exit 1
fi

# Get output name from JSON
OUTPUT=$(jq -r '.output' "$CONFIG_FILE")
if [ "$OUTPUT" = "null" ] || [ -z "$OUTPUT" ]; then
    echo "Error: Output field is missing or empty in config"
    exit 1
fi

# Initialize command array
CMD_STRING+="zig cc -fms-extensions -o $OUTPUT \\"$'\n'  # Add line continuation and newline

# Add source files
SOURCE_FILES=$(jq -r '.source_files[]' "$CONFIG_FILE")
for file in $SOURCE_FILES; do
    CMD_STRING+="$file \\"$'\n'
done

# Add libraries with tracking for pretty printing
LIB_COUNT=$(jq -r '.libraries | length' "$CONFIG_FILE")
LIB_ARGS=()
for ((i=0; i<LIB_COUNT; i++)); do
    NAME=$(jq -r ".libraries[$i].name" "$CONFIG_FILE")
    INCLUDE=$(jq -r ".libraries[$i].include" "$CONFIG_FILE")
    LIB=$(jq -r ".libraries[$i].lib" "$CONFIG_FILE")
    LINK=$(jq -r ".libraries[$i].link" "$CONFIG_FILE")
    
    LIB_LINE=()
    [ "$INCLUDE" != "null" ] && LIB_LINE+=("-I$INCLUDE") 
    [ "$LINK" = "dynamic" ] && LIB_LINE+=("-L$LIB -l$NAME")
    [ "$LINK" = "static" ] && LIB_LINE+=("$LIB")

    # Store library arguments for printing
    LIB_ARGS+=("${LIB_LINE[*]}")
    CMD_STRING+="${LIB_LINE[*]} \\"$'\n'
done

# Add warnings
WARNINGS=$(jq -r '.warnings[]' "$CONFIG_FILE")
for warning in $WARNINGS; do
    CMD_STRING+="-Werror=$warning \\"$'\n'
done

# Print the command
echo "Compile command:"
echo "$CMD_STRING"

# Execute the command string
eval "$CMD_STRING"
