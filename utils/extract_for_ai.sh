#!/bin/bash

# Extract printf statements with context for AI classification
# This script prepares data for AI model to decide appropriate log levels

if [ $# -eq 0 ]; then
    echo "Usage: $0 <file1.cpp> [file2.cpp] ..."
    echo "Extracts printf statements with context for AI classification"
    exit 1
fi

extract_with_context() {
    local file="$1"
    echo "=== FILE: $file ==="
    echo ""
    
    # Find all printf/fprintf/perror statements with more context
    grep -n -A 3 -B 3 -E "(printf|fprintf|perror)\s*\(" "$file" | \
    awk '
    BEGIN { 
        entry_num = 0
        in_entry = 0
    }
    /^[0-9]+-/ {
        # Context line before
        if (in_entry == 0) {
            entry_num++
            print "ENTRY_" entry_num ":"
            in_entry = 1
        }
        line_num = substr($0, 1, index($0, "-") - 1)
        content = substr($0, index($0, "-") + 1)
        print "  CONTEXT_BEFORE_" line_num ": " content
    }
    /^[0-9]+:/ && /(printf|fprintf|perror)\s*\(/ {
        # Main printf line
        if (in_entry == 0) {
            entry_num++
            print "ENTRY_" entry_num ":"
            in_entry = 1
        }
        line_num = substr($0, 1, index($0, ":") - 1)
        content = substr($0, index($0, ":") + 1)
        print "  MAIN_LINE_" line_num ": " content
        main_line = line_num
        main_content = content
    }
    /^[0-9]+-/ && !/(printf|fprintf|perror)\s*\(/ {
        # Context line after
        if (in_entry == 1) {
            line_num = substr($0, 1, index($0, "-") - 1)
            content = substr($0, index($0, "-") + 1)
            print "  CONTEXT_AFTER_" line_num ": " content
        }
    }
    /^--$/ {
        if (in_entry == 1) {
            print ""
            in_entry = 0
        }
    }
    END {
        if (in_entry == 1) {
            print ""
        }
    }'
    
    echo ""
}

# Create a comprehensive context file for AI processing
output_file="print_statements_for_ai.txt"
echo "# Printf/Fprintf/Perror statements extracted for AI classification" > "$output_file"
echo "# Please classify each MAIN_LINE according to the Lambda Script Logging Level Strategy:" >> "$output_file"
echo "#" >> "$output_file"
echo "# DUAL-OUTPUT LOG LEVEL MAPPING STRATEGY:" >> "$output_file"
echo "# NOTE: All logs (when enabled) go to LOG FILE + additional console outputs:" >> "$output_file"
echo "#" >> "$output_file"
echo "# 1. log_error() - Error Messages (LOG FILE + stderr)" >> "$output_file"
echo "#    - Critical errors that prevent normal operation" >> "$output_file"
echo "#    - Memory allocation failures" >> "$output_file"
echo "#    - Invalid type operations, NULL pointer dereferences" >> "$output_file"
echo "#    - Parse errors, File I/O errors" >> "$output_file"
echo "#    - Division by zero, modulo by zero" >> "$output_file"
echo "#    - Decimal operation failures" >> "$output_file"
echo "#    - Unknown/unsupported type operations" >> "$output_file"
echo "#" >> "$output_file"
echo "# 2. log_warn() - Warning Messages (LOG FILE + stderr)" >> "$output_file"
echo "#    - Recoverable issues or potential problems" >> "$output_file"
echo "#    - Type coercion warnings" >> "$output_file"
echo "#    - Deprecated feature usage" >> "$output_file"
echo "#    - Configuration fallbacks" >> "$output_file"
echo "#    - Performance concerns" >> "$output_file"
echo "#" >> "$output_file"
echo "# 3. log_info() - Informational Messages (LOG FILE + stdout)" >> "$output_file"
echo "#    - Important operational information" >> "$output_file"
echo "#    - Successful compilation phases" >> "$output_file"
echo "#    - Major operation completions" >> "$output_file"
echo "#    - Configuration loading" >> "$output_file"
echo "#    - Module imports" >> "$output_file"
echo "#" >> "$output_file"
echo "# 4. log_notice() - Major Status/Progress Messages (LOG FILE + stdout)" >> "$output_file"
echo "#    - High-level progress and status updates" >> "$output_file"
echo "#    - 'Transpiling Lambda script...'" >> "$output_file"
echo "#    - 'Building AST...'" >> "$output_file"
echo "#    - 'Loading input data...'" >> "$output_file"
echo "#    - 'Generating output...'" >> "$output_file"
echo "#" >> "$output_file"
echo "# 5. log_debug() - Debug/Trace Messages (LOG FILE + stdout)" >> "$output_file"
echo "#    - Detailed execution flow and debugging information" >> "$output_file"
echo "#    - Function entry/exit traces" >> "$output_file"
echo "#    - Variable value dumps" >> "$output_file"
echo "#    - Internal state information" >> "$output_file"
echo "#    - Memory pool operations" >> "$output_file"
echo "#    - Type inference details" >> "$output_file"
echo "#    - AST node processing traces" >> "$output_file"
echo "#    - Syntax tree printing/formatting" >> "$output_file"
echo "#" >> "$output_file"
echo "# SPECIAL NOTES:" >> "$output_file"
echo "#    - DUAL OUTPUT: All logs write to LOG FILE + designated console stream" >> "$output_file"
echo "#    - fprintf(stderr, ...) statements should become log_error()" >> "$output_file"
echo "#    - perror(...) statements should become log_error() with strerror(errno)" >> "$output_file"
echo "#    - Array length mismatches are typically log_error() (data integrity issues)" >> "$output_file"
echo "#    - Invalid pointer/field validation errors are log_error()" >> "$output_file"
echo "#    - Mathematical operation debug traces are log_debug()" >> "$output_file"
echo "#    - AST tree structure printing is log_debug()" >> "$output_file"
echo "#" >> "$output_file"
echo "# Please respond with classifications in this format:" >> "$output_file"
echo "# MAIN_LINE_123: printf(...) -> log_error" >> "$output_file"
echo "# MAIN_LINE_456: printf(...) -> log_debug" >> "$output_file"
echo "#" >> "$output_file"
echo "" >> "$output_file"

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "File not found: $file"
        continue
    fi
    
    extract_with_context "$file" >> "$output_file"
done

echo "Context data written to: $output_file"
echo ""
echo "Next steps:"
echo "1. Send the content of $output_file to an AI model with the detailed guidelines"
echo "2. Ask the AI to classify each MAIN_LINE with appropriate log level"
echo "3. Use the response with apply_ai_replacements.sh to apply changes"
echo ""
echo "Example AI prompt:"
echo "\"Please analyze these printf/fprintf/perror statements from the Lambda Script C++ codebase"
echo "and classify each MAIN_LINE with the appropriate logging level according to the detailed"
echo "Lambda Script Logging Level Strategy provided in the file header. Consider the context,"
echo "purpose, and severity of each statement. Focus on:"
echo "- Critical errors and failures -> log_error"
echo "- Recoverable issues and warnings -> log_warn" 
echo "- Important operational info -> log_info"
echo "- Major progress/status updates -> log_notice"
echo "- Debug traces and detailed execution flow -> log_debug"
echo "Please respond with each classification in the format: MAIN_LINE_N: statement -> log_level\""
