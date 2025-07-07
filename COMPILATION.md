# Unified Compilation System

This document describes the enhanced compilation system that merges the functionality of `compile-lambda.sh` and `compile-lambda-cross.sh` into a single unified script, along with dependency setup scripts for different platforms.

## Overview

The compilation system has been enhanced to support both native and cross-platform compilation through a single script with platform-specific configuration. Additionally, dedicated dependency setup scripts are provided for different platforms.

## Files

### Main Script
- `compile-lambda.sh` - Unified compilation script supporting both native and cross-compilation

### Configuration
- `build_lambda_config.json` - Main configuration file with platform-specific sections

### Dependency Setup Scripts
- `setup-mac-deps.sh` - Mac native dependency setup script
- `setup-linux-deps.sh` - Linux (Ubuntu) native dependency setup script
- `setup-windows-deps.sh` - Windows cross-compilation dependency setup script

## Linux Native Compilation (Ubuntu)

### Prerequisites

1. **Build Essential Tools**: Required for compilation
   ```bash
   sudo apt update
   sudo apt install build-essential
   ```

2. **Git and CMake**: Required for dependency building
   ```bash
   sudo apt install git cmake pkg-config
   ```

### Dependency Setup

Run the Linux dependency setup script to install all required dependencies:

```bash
./setup-linux-deps.sh
```

This script will:
- Install/build **tree-sitter** and **tree-sitter-lambda** from local sources
- Install **GMP** via apt (or build from source if apt fails)
- Build **lexbor** from source (not commonly available in Ubuntu repos)
- Build **MIR** from source (JIT compiler infrastructure)
- Build **zlog** from source (logging library, optional)

#### Dependencies Installed

- **tree-sitter**: Incremental parsing library
- **tree-sitter-lambda**: Lambda language parser
- **GMP**: GNU Multiple Precision arithmetic library (via `libgmp-dev` package)
- **lexbor**: Fast HTML/XML parsing library (built from source)
- **MIR**: Lightweight JIT compiler infrastructure (built from source)
- **zlog**: High-performance logging library (built from source, optional)

### Native Compilation

After dependencies are set up, compile the project:

```bash
./compile-lambda.sh build_lambda_config.json
```

### Clean Up

To clean intermediate build files:

```bash
./setup-linux-deps.sh clean
```

## Mac Native Compilation

### Prerequisites

1. **Xcode Command Line Tools**: Required for basic build tools
   ```bash
   xcode-select --install
   ```

2. **Homebrew** (recommended): Package manager for easier dependency installation
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

### Dependency Setup

Run the Mac dependency setup script to install all required dependencies:

```bash
./setup-mac-deps.sh
```

This script will:
- Install/build **tree-sitter** and **tree-sitter-lambda** from local sources
- Install **GMP** via Homebrew (or build from source if Homebrew fails)
- Install **lexbor** via Homebrew (or build from source if Homebrew fails)
- Build **MIR** from source (JIT compiler infrastructure)
- Build **zlog** from source (logging library, optional)

#### Dependencies Installed

- **tree-sitter**: Incremental parsing library
- **tree-sitter-lambda**: Lambda language parser
- **GMP**: GNU Multiple Precision arithmetic library
- **lexbor**: Fast HTML/XML parsing library
- **MIR**: Lightweight JIT compiler infrastructure
- **zlog**: High-performance logging library (optional)

### Native Compilation

After dependencies are set up, compile the project:

```bash
./compile-lambda.sh build_lambda_config.json
```

### Clean Up

To clean intermediate build files:

```bash
./setup-mac-deps.sh clean
```

## Windows Cross-compilation

### Prerequisites

Install MinGW-w64 cross-compiler:
```bash
brew install mingw-w64
```

### Dependency Setup

Run the Windows cross-compilation dependency setup script:

```bash
./setup-windows-deps.sh
```

### Cross-compilation

Compile for Windows:

```bash
./compile-lambda.sh --platform=windows
```

## Usage

### Linux Native Compilation (Ubuntu)
```bash
./compile-lambda.sh
```

### Mac Native Compilation (macOS)
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

The `build_lambda_config.json` includes platform-specific configurations and dependency paths:

```json
{
  "output": "lambda.exe",
  "source_files": [...],
  "cpp_files": [...],
  "libraries": [
    {
      "name": "tree-sitter",
      "include": "lambda/tree-sitter/lib/include",
      "lib": "lambda/tree-sitter/libtree-sitter.a",
      "link": "static"
    },
    {
      "name": "lexbor",
      "include": "/usr/local/include",
      "lib": "/usr/local/lib/liblexbor_static.a",
      "link": "static"
    },
    {
      "name": "gmp",
      "include": "/opt/homebrew/include",
      "lib": "/opt/homebrew/lib",
      "link": "dynamic"
    }
  ],
  "warnings": [...],
  "flags": [...],
  "debug": true,
  "build_dir": "build",
  "platforms": {
    "windows": {
      "output": "lambda-windows.exe",
      "libraries": [
        {
          "name": "tree-sitter",
          "include": "lambda/tree-sitter/lib/include",
          "lib": "lambda/tree-sitter/libtree-sitter-windows.a",
          "link": "static"
        },
        {
          "name": "lexbor",
          "include": "windows-deps/include",
          "lib": "windows-deps/lib/liblexbor_static.a",
          "link": "static"
        }
      ],
      "flags": [...],
      "linker_flags": [...],
      "build_dir": "build_windows",
      "cross_compile": true,
      "target_triplet": "x86_64-w64-mingw32"
    }
  }
}
```

## Dependency Locations

### Linux Dependency Locations

- **System libraries**: Installed to `/usr/local/lib` and `/usr/local/include`
- **APT packages**: Available at `/usr/lib` and `/usr/include` (e.g., GMP via `libgmp-dev`)
- **Local tree-sitter**: Built in `lambda/tree-sitter/` and `lambda/tree-sitter-lambda/`

### Mac Dependency Locations

- **System libraries**: Installed to `/usr/local/lib` and `/usr/local/include`
- **Homebrew libraries**: Available at `/opt/homebrew/lib` and `/opt/homebrew/include`
- **Local tree-sitter**: Built in `lambda/tree-sitter/` and `lambda/tree-sitter-lambda/`

### Windows Cross-compilation Dependencies

- **Cross-compiled libraries**: Installed to `windows-deps/lib` and `windows-deps/include`
- **Tree-sitter**: Built as `lambda/tree-sitter/libtree-sitter-windows.a`

## Platform Override Logic

When a platform is specified:
1. Platform-specific values override default values
2. If a platform-specific value doesn't exist, the default value is used
3. Arrays (like libraries, flags) are completely overridden by platform-specific versions

## Troubleshooting

### Linux Issues

1. **Missing Build Tools**:
   ```bash
   sudo apt update && sudo apt install build-essential cmake git
   ```

2. **Permission Issues**: Some system installations may require `sudo`

3. **Package Manager Issues**: 
   ```bash
   sudo apt update
   sudo apt install pkg-config libtool autoconf automake
   ```

4. **Library Path Issues**: Ensure `/usr/local/lib` is in your library path:
   ```bash
   echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/local.conf
   sudo ldconfig
   ```

### Mac Issues

1. **Missing Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

2. **Homebrew Not Found**:
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

3. **Permission Issues**: Some system installations may require `sudo`

4. **Architecture Issues**: The script automatically detects Apple Silicon vs Intel and sets appropriate flags

### Windows Cross-compilation Issues

1. **MinGW-w64 Not Found**:
   ```bash
   brew install mingw-w64
   ```

2. **Dependency Build Failures**: The Windows setup script includes fallback stub implementations

### General Issues

1. **Clean and Rebuild**:
   ```bash
   # For Linux
   ./setup-linux-deps.sh clean
   ./setup-linux-deps.sh
   
   # For Mac
   ./setup-mac-deps.sh clean
   ./setup-mac-deps.sh
   ```

2. **Check Dependencies**: The setup scripts provide detailed status reports

## Advanced Usage

### Building Individual Dependencies

You can build specific dependencies by modifying the setup scripts or using the build functions directly.

### Custom Library Paths

Modify the `build_lambda_config.json` to point to custom library installations if needed.

### Debug vs Release Builds

The configuration supports debug mode which can be toggled in the JSON configuration:

```json
{
  "debug": true  // or false for release builds
}
```


