# Lambda Script Logging Migration - AI-Assisted Workflow

## Overview

We've created an AI-assisted workflow to efficiently convert printf/fprintf/perror statements to the proper logging system based on the detailed guidelines in `vibe/Log.md`.

## Tools Created

### 1. `extract_for_ai.sh` - Context Extraction
- Extracts printf/fprintf/perror statements with surrounding context
- Includes detailed logging level strategy in output file header
- Provides comprehensive guidelines for AI classification

### 2. `apply_ai_replacements.sh` - Apply AI Recommendations  
- Parses AI response file with classifications
- Applies sed-based replacements to source files
- Creates backups before making changes
- Handles different printf patterns

### 3. `logging_workflow.sh` - Complete Workflow
- Orchestrates the entire process
- Provides step-by-step instructions
- Generates appropriate file names
- Shows exact AI prompts to use

## Usage Examples

### Single File Processing
```bash
# Step 1: Extract context
./utils/extract_for_ai.sh lambda/build_ast.cpp

# Step 2: Send print_statements_for_ai.txt to AI model with provided prompt

# Step 3: Save AI response and apply
./utils/apply_ai_replacements.sh ai_response.txt lambda/build_ast.cpp

# Step 4: Add logging include
# Add #include "../lib/log.h" to the file if needed
```

### Batch Processing with Workflow Script
```bash
# Generate context and instructions for multiple files
./utils/logging_workflow.sh lambda/build_ast.cpp lambda/transpile.cpp

# Then process each file's AI response individually
```

## AI Prompt Template

When sending context files to AI, use this prompt:

```
Please analyze these printf/fprintf/perror statements from the Lambda Script C++ codebase
and classify each MAIN_LINE with the appropriate logging level according to the detailed
Lambda Script Logging Level Strategy provided in the file header.

For each MAIN_LINE statement, consider:
- Context and purpose of the message  
- Severity and importance
- Whether it's an error, warning, info, progress update, or debug trace

Please respond with ONLY the classifications in this exact format:
MAIN_LINE_21: printf(...) -> log_debug
MAIN_LINE_134: printf(...) -> log_error
etc.

Focus on these mappings:
- Critical errors, failures, invalid operations -> log_error
- Recoverable issues, warnings, potential problems -> log_warn  
- Important operational information -> log_info
- Major progress/status updates -> log_notice
- Debug traces, internal state, execution flow -> log_debug
```

## Log Level Strategy (from Log.md)

### log_error() - Critical Errors (stderr)
- Memory allocation failures
- Invalid type operations, NULL pointer dereferences  
- Parse errors, File I/O errors
- Division by zero, modulo by zero
- Decimal operation failures
- Unknown/unsupported type operations

### log_warn() - Warnings (stderr)
- Recoverable issues or potential problems
- Type coercion warnings
- Deprecated feature usage
- Configuration fallbacks
- Performance concerns

### log_info() - Important Information (stdout)
- Important operational information
- Successful compilation phases
- Major operation completions
- Configuration loading, Module imports

### log_notice() - Major Progress (stdout)
- High-level progress and status updates
- "Transpiling Lambda script..."
- "Building AST...", "Loading input data..."
- "Generating output..."

### log_debug() - Debug/Trace (stdout)
- Detailed execution flow and debugging information
- Function entry/exit traces
- Variable value dumps, Internal state information
- Memory pool operations, Type inference details
- AST node processing traces
- Syntax tree printing/formatting

## File Processing Status

### Phase 1 Target Files:
- ✅ `lambda/lambda-mem.cpp` - Completed manually
- ✅ `lambda/print.cpp` - Completed with AI workflow  
- 🔄 `lambda/lambda-data.cpp` - Ready for AI workflow
- 🔄 `lambda/lambda-eval.cpp` - Partially completed, needs AI workflow
- 🔄 `lambda/build_ast.cpp` - Context extracted, ready for AI
- 🔄 `lambda/transpile.cpp` - Ready for AI workflow

### Next Steps:
1. Process remaining Phase 1 files using AI workflow
2. Test compilation after each file
3. Move to Phase 2 files across the codebase
4. Update build system to include log initialization

## Benefits Achieved

1. **Consistency**: AI classifies based on detailed, project-specific guidelines
2. **Efficiency**: Batch processing of statements with context
3. **Accuracy**: Human review combined with AI pattern recognition
4. **Maintainability**: Structured workflow for future logging updates
5. **Quality**: Proper separation of errors (stderr) vs debug info (stdout)

## Validation Steps

After applying changes to each file:
1. Verify `#include "../lib/log.h"` is present
2. Check compilation with `make build`
3. Test basic functionality
4. Review log output levels in development vs production modes
5. Ensure critical errors still go to stderr

## Future Enhancements

1. **Automated Testing**: Script to verify log levels work correctly
2. **Configuration**: Runtime log level switching 
3. **Performance**: Conditional expensive debug operations
4. **Integration**: Log initialization in main entry points
5. **Documentation**: Update user documentation with logging options
