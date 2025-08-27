# Lambda Script Logging Migration Tools

This directory contains the AI-assisted workflow tools for migrating printf/fprintf/perror statements to the proper logging system based on the strategy defined in `../vibe/Log.md`.

## 🛠️ Tools Overview

### `logging_workflow.sh` - Main Orchestration Script
**Purpose**: Complete workflow for converting printf statements to logging  
**Usage**: `./logging_workflow.sh <file1.cpp> [file2.cpp] ...`

**What it does**:
- Extracts printf/fprintf/perror statements with context
- Generates detailed guidelines for AI classification  
- Provides step-by-step instructions for the entire process
- Shows exact AI prompts to use

**Example**:
```bash
./logging_workflow.sh lambda/build_ast.cpp lambda/transpile.cpp
```

### `extract_for_ai.sh` - Context Extraction
**Purpose**: Extract printf statements with surrounding context for AI analysis  
**Usage**: `./extract_for_ai.sh <file1.cpp> [file2.cpp] ...`

**What it does**:
- Finds all printf/fprintf/perror statements
- Includes 3 lines of context before and after each statement
- Adds comprehensive logging level strategy guidelines
- Generates files ready for AI model consumption

**Output**: `print_statements_for_ai.txt` with detailed context and guidelines

### `apply_ai_replacements.sh` - Apply AI Recommendations
**Purpose**: Apply AI-classified logging levels to source files  
**Usage**: `./apply_ai_replacements.sh <ai_response_file> <source_file>`

**What it does**:
- Parses AI response with log level classifications
- Applies sed-based replacements to source files
- Creates automatic backups before changes
- Handles various printf/fprintf/perror patterns

**AI Response Format Expected**:
```
MAIN_LINE_123: printf(...) -> log_error
MAIN_LINE_456: printf(...) -> log_debug
```

## 📋 Complete Workflow

### Step 1: Extract Context
```bash
./utils/logging_workflow.sh lambda/your_file.cpp
```

### Step 2: AI Classification
Send the generated `your_file_for_ai.txt` to an AI model with the provided prompt.

### Step 3: Apply Changes
```bash
./utils/apply_ai_replacements.sh your_file_ai_response.txt lambda/your_file.cpp
```

### Step 4: Add Includes & Test
- Add `#include "../lib/log.h"` if needed
- Compile and test: `make build`

## 🎯 Log Level Strategy

Based on `../vibe/Log.md`:

- **`log_error()`** - Critical errors, failures, invalid operations → stderr
- **`log_warn()`** - Recoverable issues, warnings → stderr  
- **`log_info()`** - Important operational information → stdout
- **`log_notice()`** - Major progress/status updates → stdout
- **`log_debug()`** - Debug traces, execution flow → stdout

## 📁 File Status

### Phase 1 Target Files:
- ✅ `lambda/lambda-mem.cpp` - Completed
- ✅ `lambda/print.cpp` - Completed  
- 🔄 `lambda/lambda-data.cpp` - Ready for workflow
- 🔄 `lambda/lambda-eval.cpp` - Partially completed
- 🔄 `lambda/build_ast.cpp` - Ready for workflow
- 🔄 `lambda/transpile.cpp` - Ready for workflow

## 💡 Tips

1. **Batch Processing**: Use `logging_workflow.sh` with multiple files
2. **AI Prompt**: Always use the exact prompt provided by the workflow script
3. **Backup Safety**: All scripts create backups automatically
4. **Compilation**: Test compilation after each file conversion
5. **Context Matters**: The AI uses surrounding code context for better classification

## 🔍 Example AI Prompt

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

Focus on these mappings:
- Critical errors, failures, invalid operations -> log_error
- Recoverable issues, warnings, potential problems -> log_warn
- Important operational information -> log_info
- Major progress/status updates -> log_notice
- Debug traces, internal state, execution flow -> log_debug
```

## 🚀 Getting Started

For your first file conversion:

```bash
# Navigate to project root
cd /path/to/lambda

# Run the workflow (example with build_ast.cpp)
./utils/logging_workflow.sh lambda/build_ast.cpp

# Follow the step-by-step instructions provided
# Send the generated context file to AI
# Apply the AI recommendations
# Test compilation
```

This systematic approach ensures consistent, high-quality logging migration across the entire Lambda Script codebase.
