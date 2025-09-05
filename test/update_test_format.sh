#!/bin/bash

# Update test format to output variables directly
# Usage: ./test/update_test_format.sh <test_file.ls>

if [ $# -ne 1 ]; then
    echo "Usage: $0 <test_file.ls>"
    exit 1
fi

test_file="$1"
if [ ! -f "$test_file" ]; then
    echo "Error: Test file $test_file not found"
    exit 1
fi

# Create a temporary file for the updated test
updated_file="${test_file}.updated"
: > "$updated_file"

# Process the test file
in_header=true
while IFS= read -r line; do
    # Copy header comments
    if [[ "$in_header" == true && ("$line" == "//"* || "$line" == "" || "$line" =~ ^[[:space:]]*$) ]]; then
        echo "$line" >> "$updated_file"
    else
        in_header=false
        # Skip empty lines and comments
        if [[ ! "$line" =~ ^[[:space:]]*$ && ! "$line" =~ ^[[:space:]]*// ]]; then
            # Check if line is a variable assignment
            if [[ "$line" =~ ^[[:space:]]*let[[:space:]]+([a-zA-Z_][a-zA-Z0-9_]*)[[:space:]]*= ]]; then
                var_name="${BASH_REMATCH[1]}"
                # Remove any trailing comments
                line_clean="${line%%//*}"
                # Add the original line
                echo "$line_clean" >> "$updated_file"
                # Add the variable output
                echo -e "$var_name" >> "$updated_file"
            else
                # Keep other lines as is
                echo "$line" >> "$updated_file"
            fi
        fi
    fi
done < "$test_file"

# Create the expected output file
expected_file="${test_file%.ls}.expected"
echo "Creating expected output file: $expected_file"
./lambda.exe "$test_file" > "$expected_file" 2>&1

# Replace the original file with the updated one
mv "$updated_file" "$test_file"
echo "Updated test file: $test_file"
