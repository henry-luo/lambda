# Unified Compilation System

This document describes the compilation system that provides a single unified script for building both the Lambda and Radiant projects, supporting both native and cross-platform compilation.

## Files

### Main Script
- `compile.sh` - Unified compilation script supporting both Lambda and Radiant projects with native and cross-compilation capabilities

### Configuration
- `build_lambda_config.json` - Configuration file for the lambda project with platform-specific sections
- `build_radiant_config.json` - Configuration file for the radiant sub-project

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
./compile.sh
```

### Clean Up

To clean intermediate build files:

```bash
./setup-linux-deps.sh clean
```

## Mac Native Compilation

### Prerequisites

1. **Homebrew** (recommended): Package manager for easier dependency installation
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
./compile.sh
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

### Cross-compilation Dependency Location

- **Cross-compiled libraries**: Installed to `windows-deps/lib` and `windows-deps/include`
- **Tree-sitter**: Built as `lambda/tree-sitter/libtree-sitter-windows.a`

### Cross-compilation

Compile for Windows:

```bash
./compile.sh --platform=windows
```

## Usage

### Building Lambda Project

**Linux Native Compilation (Ubuntu):**
```bash
./compile.sh
```

**Mac Native Compilation (macOS):**
```bash
./compile.sh
```

**Cross-compilation for Windows:**
```bash
./compile.sh --platform=windows
```

### Building Radiant Project

**Mac Native Compilation (macOS):**
```bash
./compile.sh build_radiant_config.json
```

### Help
```bash
./compile.sh --help
```

## Configuration Structure

The build system uses JSON configuration files for each project:

### Lambda Configuration (`build_lambda_config.json`)

The lambda configuration includes platform-specific configurations and dependency paths:

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

## Troubleshooting

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

### Debug vs Release Builds

The configuration supports debug mode which can be toggled in the JSON configuration:

```json
{
  "debug": true  // or false for release builds
}
```


