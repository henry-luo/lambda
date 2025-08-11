# Parser Debug Guide

## Overview
This document outlines common parser hang issues, prevention strategies, and debugging techniques based on findings from fixing the AsciiDoc parser infinite loop problems.

## Common Causes of Parser Hangs

### 1. Infinite Loops in Parsing Logic
**Root Cause**: Parser logic that can iterate indefinitely without proper termination conditions.

**Examples**:
- Table parsing with ambiguous delimiters (`|===` in AsciiDoc)
- Complex nested markup processing without bounds checking
- Malformed input causing unexpected parser states

**Symptoms**:
- Process consumes 100% CPU but produces no output
- Parser never returns from specific parsing functions
- Memory usage may grow unbounded

### 2. Recursive Function Calls Without Base Cases
**Root Cause**: Recursive parsing functions that lack proper termination conditions.

**Examples**:
- Inline formatting parsers calling themselves indefinitely
- Nested structure parsing without depth limits
- Cross-referencing parsing logic creating circular dependencies

**Symptoms**:
- Stack overflow errors (if caught)
- Gradual memory consumption increase
- Parser hangs on specific input patterns

### 3. State Machine Issues
**Root Cause**: Parser state machines that can enter invalid or circular states.

**Examples**:
- State transitions without proper exit conditions
- Complex markup combinations causing state conflicts
- Parser unable to advance position in input stream

**Symptoms**:
- Parser stuck at specific input position
- Repeated processing of same content
- No forward progress in parsing

## Prevention Strategies

### 1. Add Loop Safety Checks
```cpp
// Example: Add iteration limits to prevent infinite loops
void parse_table(const char* input, size_t len) {
    size_t max_iterations = 10000;  // Reasonable limit
    size_t iteration_count = 0;
    
    while (condition && iteration_count < max_iterations) {
        // parsing logic
        iteration_count++;
    }
    
    if (iteration_count >= max_iterations) {
        // Log error and exit gracefully
        fprintf(stderr, "Warning: Table parsing hit iteration limit\n");
        return;
    }
}
```

### 2. Implement Recursion Depth Limits
```cpp
// Example: Limit recursion depth in inline parsing
#define MAX_RECURSION_DEPTH 50

ParseResult parse_inline(const char* input, int depth) {
    if (depth > MAX_RECURSION_DEPTH) {
        // Return simplified result instead of recursing further
        return parse_as_plain_text(input);
    }
    
    // Normal parsing logic with depth + 1 for recursive calls
}
```

### 3. Add Input Position Tracking
```cpp
// Example: Ensure parser always advances position
void parse_content(const char* input, size_t* pos, size_t len) {
    size_t start_pos = *pos;
    
    // parsing logic that should advance *pos
    
    if (*pos == start_pos) {
        // Parser didn't advance - prevent infinite loop
        (*pos)++;  // Force advancement
        fprintf(stderr, "Warning: Parser forced to advance at position %zu\n", start_pos);
    }
}
```

### 4. Implement Timeouts
```cpp
// Example: Add timeout protection for complex parsing
#include <time.h>

bool parse_with_timeout(const char* input, double timeout_seconds) {
    clock_t start_time = clock();
    
    while (parsing_condition) {
        // parsing logic
        
        // Check timeout periodically
        if ((clock() - start_time) / CLOCKS_PER_SEC > timeout_seconds) {
            fprintf(stderr, "Warning: Parsing timed out after %.2f seconds\n", timeout_seconds);
            return false;  // Timeout reached
        }
    }
    return true;  // Completed normally
}
```

## Debugging Techniques

### 1. Use External Timeouts for Testing
```bash
# Use timeout command to prevent indefinite hangs during testing
timeout 15s ./parser test_file.adoc

# If command times out, you know there's a hang issue
if [ $? -eq 124 ]; then
    echo "Parser timed out - likely infinite loop"
fi
```

### 2. Add Debug Logging
```cpp
// Example: Add position tracking logs
void debug_parse_position(const char* input, size_t pos, const char* function_name) {
    #ifdef DEBUG_PARSER
    fprintf(stderr, "[DEBUG] %s: position %zu, char: '%c' (0x%02x)\n", 
            function_name, pos, 
            pos < strlen(input) ? input[pos] : '\0',
            pos < strlen(input) ? (unsigned char)input[pos] : 0);
    #endif
}

// Use in parsing loops:
while (condition) {
    debug_parse_position(input, current_pos, "parse_table");
    // parsing logic
}
```

### 3. Create Minimal Test Cases
```bash
# Create progressively smaller test files to isolate the problem
cp comprehensive_test.adoc debug_full.adoc
head -n 100 comprehensive_test.adoc > debug_medium.adoc  
head -n 10 comprehensive_test.adoc > debug_small.adoc

# Test each to find the minimum hanging case
timeout 10s ./parser debug_small.adoc    # Should work
timeout 10s ./parser debug_medium.adoc   # May hang
timeout 10s ./parser debug_full.adoc     # Definitely hangs
```

### 4. Use Process Monitoring
```bash
# Monitor CPU and memory usage to identify hangs
# In one terminal:
./parser problematic_file.adoc &
PARSER_PID=$!

# In another terminal:
while kill -0 $PARSER_PID 2>/dev/null; do
    ps -p $PARSER_PID -o pid,ppid,pcpu,pmem,time,command
    sleep 1
done
```

### 5. Add Checkpoints in Code
```cpp
// Example: Add checkpoint logging to track progress
#define CHECKPOINT(msg) fprintf(stderr, "CHECKPOINT: %s at line %d\n", msg, __LINE__)

void parse_complex_structure() {
    CHECKPOINT("Starting table parse");
    
    while (parsing_table) {
        CHECKPOINT("Processing table row");
        // table parsing logic
    }
    
    CHECKPOINT("Table parse complete");
    
    CHECKPOINT("Starting inline parse");
    // inline parsing logic
    CHECKPOINT("Inline parse complete");
}
```

## Specific Fixes Applied to AsciiDoc Parser

### 1. Table Parser Fix
**Problem**: Infinite loop on ambiguous `|===` delimiters
**Solution**: Added iteration limits and position advancement checks

### 2. Inline Parser Fix
**Problem**: Recursive inline formatting causing infinite recursion
**Solution**: Simplified to plain text processing, removed complex recursion

### 3. Safety Measures Added
- Loop iteration counters with maximum limits
- Position advancement verification
- Graceful degradation (plain text fallback)
- Timeout-based testing infrastructure

## Testing Strategy for Parser Robustness

### 1. Automated Timeout Testing
```bash
#!/bin/bash
# test_parser_robustness.sh

TIMEOUT=10  # seconds
PARSER=./lambda.exe

for test_file in test/input/*.adoc; do
    echo "Testing $test_file..."
    if timeout $TIMEOUT $PARSER "$test_file" > /dev/null 2>&1; then
        echo "  ✓ PASS: Completed within timeout"
    else
        if [ $? -eq 124 ]; then
            echo "  ✗ FAIL: Timed out (possible hang)"
        else
            echo "  ✗ FAIL: Parser error"
        fi
    fi
done
```

### 2. Incremental Complexity Testing
- Start with minimal valid input
- Gradually add complexity
- Identify the exact feature causing hangs
- Isolate and fix the problematic parsing logic

### 3. Malformed Input Testing
- Test with deliberately malformed markup
- Ensure parser handles edge cases gracefully
- Verify no infinite loops on invalid input

## Recommendations

1. **Always add loop limits** to any parsing loop that processes user input
2. **Implement recursion depth limits** for any recursive parsing functions  
3. **Use timeouts during development** to catch infinite loops early
4. **Add position advancement checks** to ensure parser makes progress
5. **Create comprehensive test suites** with both valid and malformed input
6. **Implement graceful degradation** - better to parse as plain text than hang
7. **Use external monitoring tools** during debugging to identify hang patterns
8. **Document known problematic patterns** for future maintenance

## Conclusion

Parser hangs are typically caused by insufficient bounds checking and missing termination conditions. Prevention through proactive safety measures is more effective than reactive debugging. When debugging hangs, use external timeouts, incremental testing, and comprehensive logging to isolate the root cause quickly.
