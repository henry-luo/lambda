#!/bin/bash

# Apply AI-recommended log level changes
# Usage: ./apply_ai_replacements.sh <ai_response_file> <source_file>

if [ $# -ne 2 ]; then
    echo "Usage: $0 <ai_response_file> <source_file>"
    echo ""
    echo "The AI response file should contain lines like:"
    echo "MAIN_LINE_123: printf(...) -> log_error"
    echo "MAIN_LINE_456: printf(...) -> log_debug"
    echo "etc."
    exit 1
fi

ai_response_file="$1"
source_file="$2"

if [ ! -f "$ai_response_file" ]; then
    echo "AI response file not found: $ai_response_file"
    exit 1
fi

if [ ! -f "$source_file" ]; then
    echo "Source file not found: $source_file"
    exit 1
fi

# Create backup
cp "$source_file" "${source_file}.backup_$(date +%Y%m%d_%H%M%S)"

echo "Processing AI recommendations from $ai_response_file"
echo "Applying changes to $source_file"
echo ""

# Parse AI response and generate sed commands
while IFS= read -r line; do
    # Skip comments and empty lines
    [[ "$line" =~ ^#.*$ ]] && continue
    [[ -z "$line" ]] && continue
    
    # Extract line number and recommendation
    if [[ "$line" =~ MAIN_LINE_([0-9]+):.*-\>.*log_(error|warn|info|notice|debug) ]]; then
        line_num="${BASH_REMATCH[1]}"
        log_level="${BASH_REMATCH[2]}"
        
        echo "Line $line_num -> log_$log_level"
        
        # Get the original line content
        original_line=$(sed -n "${line_num}p" "$source_file")
        
        if [[ -z "$original_line" ]]; then
            echo "  Warning: Line $line_num not found in source file"
            continue
        fi
        
        # Skip sprintf/snprintf lines (these are string formatting, not logging)
        if [[ "$original_line" =~ (sprintf|snprintf)\( ]]; then
            echo "  Skipping: Line $line_num contains sprintf/snprintf (string formatting, not logging)"
            continue
        fi
        
        # Extract the original indentation (leading whitespace)
        original_indent=$(echo "$original_line" | sed 's/[^ \t].*//')
        
        # Generate replacement based on the printf pattern
        if [[ "$original_line" =~ printf\( ]]; then
            # Handle printf statements
            if [[ "$original_line" =~ printf\(\"([^\"]*)\",(.+)\) ]]; then
                # printf with format and arguments
                format_string="${BASH_REMATCH[1]}"
                args="${BASH_REMATCH[2]}"
                # Remove trailing \n from format string
                format_string=$(echo "$format_string" | sed 's/\\n$//')
                replacement="log_${log_level}(\"${format_string}\",$args)"
            elif [[ "$original_line" =~ printf\(\"([^\"]*)\"\) ]]; then
                # printf with just format string
                format_string="${BASH_REMATCH[1]}"
                format_string=$(echo "$format_string" | sed 's/\\n$//')
                replacement="log_${log_level}(\"${format_string}\")"
            else
                echo "  Warning: Could not parse printf pattern in line $line_num"
                continue
            fi
            
        elif [[ "$original_line" =~ fprintf\(stderr ]]; then
            # Handle fprintf to stderr
            if [[ "$original_line" =~ fprintf\(stderr,(.+)\) ]]; then
                args="${BASH_REMATCH[1]}"
                replacement="log_${log_level}($args)"
            else
                echo "  Warning: Could not parse fprintf pattern in line $line_num"
                continue
            fi
            
        elif [[ "$original_line" =~ perror\(\"([^\"]*)\"\) ]]; then
            # Handle perror statements
            message="${BASH_REMATCH[1]}"
            replacement="log_${log_level}(\"${message}: %s\", strerror(errno))"
            
        else
            echo "  Warning: Unrecognized print statement pattern in line $line_num"
            continue
        fi
        
        # Create the full replacement line with preserved indentation
        full_replacement="${original_indent}${replacement};"
        
        # Apply the replacement using sed
        # Escape special characters for sed
        escaped_original=$(printf '%s\n' "$original_line" | sed 's/[[\.*^$()+?{|]/\\&/g')
        escaped_replacement=$(printf '%s\n' "$full_replacement" | sed 's/[[\.*^$(){}]/\\&/g')
        
        # Use sed to replace the line, preserving original indentation
        sed -i.tmp "${line_num}s|.*|$escaped_replacement|" "$source_file"
        
        echo "  Applied: $replacement"
        
    fi
    
done < "$ai_response_file"

# Clean up temporary file
rm -f "${source_file}.tmp"

echo ""
echo "Replacements complete. Backup saved as ${source_file}.backup_*"
echo "Don't forget to add #include \"../lib/log.h\" to the file if not already present"
