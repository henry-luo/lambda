# Compilation System

A unified build system for Lambda and Radiant projects with incremental builds, parallel compilation, and cross-platform support.

## Files Overview

**Scripts:**
- `compile.sh` - Main compilation script with advanced features
- `Makefile` - Standard make interface wrapper
- `setup-{mac,linux,windows}-deps.sh` - Platform-specific dependency installers

**Configuration:**
- `build_lambda_config.json` - Lambda project configuration
- `build_radiant_config.json` - Radiant project configuration

**Dependencies:**
- **tree-sitter**: Incremental parsing library
- **GMP**: GNU Multiple Precision arithmetic library  
- **lexbor**: Fast HTML/XML parsing library
- **MIR**: Lightweight JIT compiler infrastructure
- **zlog**: High-performance logging library (optional)

## Quick Start

### Prerequisites by Platform

**Linux (Ubuntu):**
```bash
sudo apt update
sudo apt install build-essential git cmake pkg-config
```

**macOS:**
```bash
# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

**Windows Cross-compilation:**
```bash
brew install mingw-w64  # On macOS/Linux host
```

### Dependency Setup

```bash
# Run the appropriate script for your platform
./setup-linux-deps.sh     # Linux
./setup-mac-deps.sh       # macOS  
./setup-windows-deps.sh   # Windows cross-compilation
```

### Building

**Recommended (using Makefile):**
```bash
make                    # Build Lambda project
make radiant           # Build Radiant project
make all               # Build both projects
make cross-compile     # Cross-compile for Windows
```

**Direct script usage:**
```bash
./compile.sh                        # Incremental build
./compile.sh --platform=windows     # Cross-compile for Windows
./compile.sh --debug               # Debug build
./compile.sh --force               # Force rebuild
```

## Build Options

### Makefile Targets (Recommended)

**Primary targets:**
```bash
make help           # Show all available targets
make build          # Incremental build (default)
make debug          # Debug build
make rebuild        # Force complete rebuild  
make clean          # Clean build artifacts
make test           # Run tests
make run            # Build and run
make cross-compile  # Cross-compile for Windows
```

**Project-specific:**
```bash
make lambda         # Build Lambda project
make radiant        # Build Radiant project  
make all            # Build all projects
```

**Advanced:**
```bash
make build JOBS=4   # Build with 4 parallel jobs
make info           # Show project information
make time-build     # Benchmark build performance
```

### compile.sh Options

**Performance:**
```bash
./compile.sh --jobs=N         # Specify parallel jobs (auto-detects by default)
./compile.sh --force          # Force complete rebuild
./compile.sh --debug          # Debug build with AddressSanitizer
./compile.sh -j 8 --force     # Maximum parallel rebuild
```

**Configuration:**
```bash
./compile.sh config_file.json              # Use custom config
./compile.sh --platform=windows            # Cross-compile
./compile.sh --platform=debug              # Debug platform
./compile.sh config.json --platform=linux  # Platform with custom config
```

### Development Workflow

```bash
make                # Quick incremental build
make test          # Run tests
make run           # Build and run
make clean debug   # Clean debug build
make rebuild       # Force complete rebuild
```

## Advanced Features

### Incremental Compilation
- **Precise dependency tracking** using `.d` files with `-MMD -MP` flags
- 5-10x faster builds when few files changed
- 50-100x faster for header-only changes
- Automatic dependency management without manual intervention

```bash
./compile.sh               # Uses .d files + header cache
./compile.sh --clean-deps  # Clean dependency files
./compile.sh --force       # Force full rebuild
```

### Parallel Compilation
- Auto-detects CPU cores (max 8 jobs for stability)
- 2-4x faster compilation on multi-core systems
- Smart job limiting prevents system overload

```bash
./compile.sh --jobs=4      # Manual job control
make build JOBS=6          # Via Makefile
```

### Cross-Platform Support
- **macOS**: Native with clang
- **Linux**: Native with gcc/clang  
- **Windows**: Cross-compilation with MinGW-w64
- Automatic compiler selection and platform optimization

## Configuration

### JSON Configuration Structure

Both projects use JSON configuration files with platform-specific overrides:

```json
{
  "output": "lambda.exe",
  "source_files": ["lambda/main.cpp", "lib/strbuf.c"],
  "source_dirs": ["lambda/input", "lambda/format"],
  "libraries": [
    {
      "name": "tree-sitter",
      "include": "lambda/tree-sitter/lib/include", 
      "lib": "lambda/tree-sitter/libtree-sitter.a",
      "link": "static"
    }
  ],
  "build_dir": "build",
  "warnings": [...],
  "flags": [...],
  "debug": true,
  "platforms": {
    "windows": {
      "output": "lambda-windows.exe",
      "build_dir": "build_windows",
      "cross_compile": true,
      "target_triplet": "x86_64-w64-mingw32"
    }
  }
}
```

### Source Discovery

**Automatic file detection:**
- **C files**: `.c`
- **C++ files**: `.cpp`, `.cc`, `.cxx`, `.c++`, `.C`, `.CPP`
- **Directory scanning**: Recursively finds all source files in `source_dirs`
- **Unified compilation**: Automatically selects appropriate compiler per file type

**Platform-specific sources:**
```json
{
  "source_dirs": ["common", "shared"],
  "platforms": {
    "windows": {
      "source_dirs": ["windows-specific", "platform/win32"]
    }
  }
}
```

## Utilities

### Performance Monitoring
```bash
make time-build             # Time a single build
make info                   # Show project information
./compile.sh --help         # Show all script options
```

### Troubleshooting
```bash
make clean                  # Clean build artifacts
./compile.sh --clean-deps   # Clean dependency files
./setup-{platform}-deps.sh clean  # Clean dependencies
```

## Build System Comparison

### Our Custom System Strengths
- **JSON configuration** - Clean, readable format with `source_dirs` support
- **Cross-compilation** - Built-in Windows support via MinGW
- **Precise dependency tracking** - `.d` file generation for accurate incremental builds
- **Parallel compilation** - Auto CPU detection with job limiting
- **Automatic source discovery** - Directory-based file inclusion
- **Modern UX** - Colored output, clickable links, build summaries
- **Transparency** - Clear shell commands vs generated makefiles
- **Zero dependencies** - Pure shell script

### Comparison Summary

| Feature | Our Script | CMake | Make | Meson |
|---------|------------|-------|------|-------|
| **Setup Time** | ✅ Instant | ❌ Complex | ✅ Quick | ⚠️ Medium |
| **Cross-compilation** | ✅ Excellent | ✅ Good | ❌ Manual | ✅ Excellent |
| **Readability** | ✅ JSON | ❌ Complex | ❌ Cryptic | ✅ Python-like |
| **Scalability** | ⚠️ <1000 files | ✅ Unlimited | ✅ Good | ✅ Excellent |
| **IDE Integration** | ❌ None | ✅ Excellent | ⚠️ Basic | ✅ Good |

### Best Use Case
**Small-medium projects** (<1000 files) requiring cross-compilation, rapid prototyping, and modern developer experience without build system complexity.

### Performance Characteristics
- **Parallel builds** with smart job limiting (max 8)
- **Precise incremental builds** via `.d` dependency tracking
- **5-10x faster** builds for few file changes
- **50-100x faster** for header-only changes
- Optimal for current project size (~40 source files)


