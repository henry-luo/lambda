# Lambda Build System Migration

This document describes the migration from shell-based compilation (`compile.sh`) to the Premake5-based build system.

## Overview

The Lambda project now supports building both the main Lambda program and all tests using a unified Premake5-based build system, while maintaining compatibility with the existing JSON configuration structure.

## Build System Components

### 1. Enhanced Generator Script
- `utils/generate_premake.py` - Generates complete Premake configuration for both main program and tests
- Reads from `build_lambda_config.json` to extract build requirements
- Supports all library dependencies, source directories, and compiler flags

### 2. Generated Configuration
- `premake5.lua` - Generated Premake configuration (DO NOT EDIT MANUALLY)
- Includes main Lambda program, library projects, and test executables
- Automatically handles C/C++ mixed-language compilation

## Usage

### Generate Premake Configuration
```bash
python3 utils/generate_premake.py
```

### Generate Makefiles
```bash
premake5 gmake
```

### Build Main Program
```bash
# Debug build
make -C build/premake config=debug_x64 lambda

# Release build  
make -C build/premake config=release_x64 lambda
```

### Build Tests
```bash
# Build specific test
make -C build/premake config=debug_x64 test_strbuf

# Build all projects
make -C build/premake config=debug_x64
```

### Build All (Main + Tests)
```bash
make -C build/premake config=debug_x64
```

## Configuration Structure

The build system reads from `build_lambda_config.json` and processes:

- **Main Program**: `output`, `source_files`, `source_dirs`
- **Libraries**: External dependencies with static/dynamic linking
- **Development Libraries**: Additional libraries like GiNaC for math processing
- **Test Suites**: Individual test executables with their dependencies

## Key Features

### Mixed Language Support
- Automatically detects C and C++ files
- Applies appropriate compiler flags (`-std=c99` for C, `-std=c++17` for C++)
- Handles mixed-language projects correctly

### External Library Integration
- Static library linking via `linkoptions`
- Dynamic library linking via `links`
- macOS framework support
- Automatic dependency resolution

### Source Directory Scanning
- Explicit file inclusion from `source_dirs` using glob patterns
- Recursive scanning of `lambda/input/**/*.cpp` and `lambda/format/**/*.cpp`
- Ensures all formatter and input parser functions are included

## Migration Benefits

1. **Consistency**: Single configuration source for all build targets
2. **IDE Integration**: Native support in Visual Studio, Code::Blocks, etc.
3. **Cross-Platform**: Premake supports multiple platforms and generators
4. **Dependency Management**: Better handling of complex library dependencies
5. **Incremental Builds**: Proper dependency tracking and incremental compilation

## Example Build Output

```bash
$ make -C build/premake config=debug_x64 lambda
==== Building lambda (debug_x64) ====
Creating ../obj/lambda/x64/Debug
[compilation output...]
Linking lambda
$ ls lambda.exe
-rwxr-xr-x@ 1 user staff 13027928 Sep 13 22:43 lambda.exe
```