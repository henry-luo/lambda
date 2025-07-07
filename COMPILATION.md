# Unified Compilation System

This document describes the enhanced compilation system that merges the functionality of `compile-lambda.sh` and `compile-lambda-cross.sh` into a single unified script.

## Overview

The compilation system has been enhanced to support both native and cross-platform compilation through a single script with platform-specific configuration.

## Files

### Main Script
- `compile-lambda.sh` - Unified compilation script supporting both native and cross-compilation

### Configuration
- `build_lambda_config.json` - Main configuration file with platform-specific sections

## Usage

### Native Compilation (macOS/Linux)
```bash
./compile-lambda.sh
```

### Cross-compilation for Windows
```bash
./compile-lambda.sh --platform=windows
```

### Using Custom Configuration
```bash
./compile-lambda.sh custom_config.json --platform=windows
```

### Help
```bash
./compile-lambda.sh --help
```

## Configuration Structure

The `build_lambda_config.json` now includes a `platforms` section for platform-specific configurations:

```json
{
  "output": "lambda.exe",
  "source_files": [...],
  "cpp_files": [...],
  "libraries": [...],
  "warnings": [...],
  "flags": [...],
  "debug": true,
  "build_dir": "build",
  "platforms": {
    "windows": {
      "output": "lambda-windows.exe",
      "libraries": [...],
      "flags": [...],
      "linker_flags": [...],
      "build_dir": "build_windows",
      "cross_compile": true,
      "target_triplet": "x86_64-w64-mingw32"
    }
  }
}
```

## Platform Override Logic

When a platform is specified:
1. Platform-specific values override default values
2. If a platform-specific value doesn't exist, the default value is used
3. Arrays (like libraries, flags) are completely overridden by platform-specific versions

## Cross-compilation Requirements

For Windows cross-compilation, ensure MinGW-w64 is installed:
```bash
brew install mingw-w64
```

The script will automatically detect and use the appropriate cross-compiler based on the target triplet.


