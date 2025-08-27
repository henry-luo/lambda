# Utils Directory

This directory contains utility scripts and tools for the Lambda Script project.

## 🚀 Logging Migration Tools

**Complete AI-assisted workflow for migrating printf statements to proper logging system.**

See **[Printf_Migration.md](Printf_Migration.md)** for comprehensive documentation.

### Quick Start
```bash
# Convert printf statements to proper logging
./utils/logging_workflow.sh lambda/your_file.cpp

# Follow the step-by-step AI-assisted process
```

**Key Tools:**
- `logging_workflow.sh` - Main orchestration script
- `extract_for_ai.sh` - Extract printf statements with context  
- `apply_ai_replacements.sh` - Apply AI-classified log levels

## 🛠️ Development Scripts

### `update_intellisense.sh`
Updates VS Code IntelliSense database by regenerating `compile_commands.json`.

**Usage:**
```bash
# From project root
./utils/update_intellisense.sh

# Or using make target
make intellisense

# Or from utils directory
cd utils && ./update_intellisense.sh
```

**Features:**
- Automatically detects project root directory
- Cleans previous build to ensure all files are included
- Uses Bear to capture compilation commands
- Provides helpful output and instructions

### `build_core.sh` & `build_utils.sh`
Build utilities for the project build system.

### `generate_premake.py`
Python script for generating Premake5 build configurations.

### `empty.cpp`
Empty C++ file used in build configurations.

## VS Code Integration

The IntelliSense update script is integrated with VS Code:
- **Task**: Use Cmd+Shift+P → "Tasks: Run Task" → "Update IntelliSense"
- **Makefile**: Run `make intellisense`
- **Manual**: Run `./utils/update_intellisense.sh`

## Requirements

- **Bear**: Install with `brew install bear` (for compile_commands.json generation)
- **jq**: For JSON parsing (optional, improves output formatting)
