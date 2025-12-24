# Clang-Based Refactoring Tool

Robust AST-based refactoring tool to convert `printf()` and `fprintf(stderr, ...)` calls to `log_debug()`.

## Features

- **AST-based parsing**: Uses libclang to properly parse C/C++ code
- **Accurate matching**: Correctly identifies function calls using the Abstract Syntax Tree
- **Safe refactoring**: Handles nested calls, macros, and complex expressions
- **Include management**: Automatically adds `#include "log.h"` when needed
- **Preview mode**: Dry-run option to preview changes before applying

## Building

### macOS
```bash
# Install LLVM via Homebrew if not already installed
brew install llvm

# Build the tool
cd build/lumin
chmod +x build.sh
./build.sh
```

### Linux
```bash
# Install libclang development files
sudo apt-get install libclang-dev

# Build the tool
cd build/lumin
chmod +x build.sh
./build.sh
```

## Usage

```bash
# Preview changes without modifying the file
./refactor_to_log_debug ../../include/mir-bitmap.h --dry-run

# Apply changes with backup
./refactor_to_log_debug ../../include/mir-bitmap.h --backup

# Apply changes directly
./refactor_to_log_debug ../../include/mir-bitmap.h
```

## How It Works

1. **Parse**: Uses libclang to parse the C/C++ source file into an AST
2. **Match**: Traverses the AST to find `CallExpr` nodes matching:
   - `printf(...)` calls
   - `fprintf(stderr, ...)` calls
3. **Transform**: Replaces matched calls with `log_debug(...)`
4. **Include**: Adds `#include "log.h"` if not already present
5. **Write**: Applies all replacements and writes the result

## Advantages Over Regex

- **Context-aware**: Understands C/C++ syntax and semantics
- **Type-safe**: Verifies function signatures and arguments
- **Robust**: Handles complex cases like:
  - Multi-line function calls
  - Nested parentheses and function calls
  - String literals with escaped characters
  - Macro expansions
  - Comments within code

## Example

**Before:**
```c
fprintf(stderr, "wrong %s for a bitmap", op);
printf("Debug value: %d\n", x);
```

**After:**
```c
log_debug("wrong %s for a bitmap", op);
log_debug("Debug value: %d\n", x);
```

## Troubleshooting

If build fails:
- Ensure LLVM/Clang development libraries are installed
- Check that `clang++` is in your PATH
- Verify libclang is available: `ldconfig -p | grep libclang` (Linux) or `brew list llvm` (macOS)
