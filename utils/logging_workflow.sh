#!/bin/bash

# Complete workflow for converting printf statements to logging
# Usage: ./logging_workflow.sh <file1.cpp> [file2.cpp] ...

if [ $# -eq 0 ]; then
    echo "Usage: $0 <file1.cpp> [file2.cpp] ..."
    echo ""
    echo "This script will:"
    echo "1. Extract printf/fprintf/perror statements with context"
    echo "2. Generate detailed guidelines for AI classification"
    echo "3. Provide instructions for getting AI recommendations"
    echo "4. Show how to apply the AI recommendations"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Ensure temp directory exists
mkdir -p "$PROJECT_ROOT/temp"

echo "=== Lambda Script Logging Conversion Workflow ==="
echo ""

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "File not found: $file"
        continue
    fi
    
    # Get filename without path and extension for outputs
    basename_file=$(basename "$file" .cpp)
    
    echo "Processing: $file"
    echo "----------------------------------------"
    
    # Step 1: Extract statements with context
    echo "Step 1: Extracting printf statements with context..."
    context_file="temp/${basename_file}_for_ai.txt"
    "$SCRIPT_DIR/extract_for_ai.sh" "$file" > /dev/null 2>&1
    mv print_statements_for_ai.txt "$context_file"
    echo "  Context data saved to: $context_file"
    
    # Step 2: Count statements to process
    statement_count=$(grep -c "MAIN_LINE_" "$context_file")
    echo "  Found $statement_count printf/fprintf/perror statements"
    
    # Step 3: Show AI prompt
    echo ""
    echo "Step 2: AI Classification Instructions"
    echo "======================================" 
    echo ""
    echo "Please send the content of '$context_file' to an AI model with this prompt:"
    echo ""
    echo "------- AI PROMPT START -------"
    echo "Please analyze these printf/fprintf/perror statements from the Lambda Script C++ codebase"
    echo "and classify each MAIN_LINE with the appropriate logging level according to the detailed"
    echo "Lambda Script Logging Level Strategy provided in the file header."
    echo ""
    echo "For each MAIN_LINE statement, consider:"
    echo "- Context and purpose of the message"
    echo "- Severity and importance"
    echo "- Whether it's an error, warning, info, progress update, or debug trace"
    echo ""
    echo "Please respond with ONLY the classifications in this exact format:"
    echo "MAIN_LINE_21: printf(...) -> log_debug"
    echo "MAIN_LINE_134: printf(...) -> log_error"
    echo "etc."
    echo ""
    echo "Focus on these mappings:"
    echo "- Critical errors, failures, invalid operations -> log_error"
    echo "- Recoverable issues, warnings, potential problems -> log_warn"
    echo "- Important operational information -> log_info"
    echo "- Major progress/status updates -> log_notice"
    echo "- Debug traces, internal state, execution flow -> log_debug"
    echo "------- AI PROMPT END -------"
    echo ""
    
    # Step 4: Instructions for applying changes
    echo "Step 3: Apply AI Recommendations"
    echo "================================"
    echo ""
    echo "After getting AI response:"
    echo "1. Save the AI response to: temp/${basename_file}_ai_response.txt"
    echo "2. Run: $SCRIPT_DIR/apply_ai_replacements.sh temp/${basename_file}_ai_response.txt $file"
    echo "3. Check if $file needs #include \"../lib/log.h\" added"
    echo "4. Verify the changes and compile to ensure correctness"
    echo ""
    
    echo "Files generated:"
    echo "  - $context_file (send this to AI)"
    echo "  - temp/${basename_file}_ai_response.txt (save AI response here)"
    echo ""
done

echo "=== Workflow Complete ==="
echo ""
echo "Summary of steps:"
echo "1. ✓ Context extraction completed"
echo "2. → Send context files to AI for classification"  
echo "3. → Apply AI recommendations using apply_ai_replacements.sh"
echo "4. → Add log.h includes where needed"
echo "5. → Test compilation"
echo ""
echo "For batch processing multiple files, repeat this workflow for each file."
