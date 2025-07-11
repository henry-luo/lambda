# The Compilation System

This document describes the compilation system that provides a unified script for building both the Lambda and Radiant projects, supporting native and cross-platform compilation with incremental builds, parallel compilation, and comprehensive build automation.

## Files

### Main Script
- `compile.sh` - Advanced unified compilation script with incremental builds, parallel compilation, and cross-platform support
- `Makefile` - Standard make interface that utilizes compile.sh for common build targets

### Configuration
- `build_lambda_config.json` - Main configuration file with platform-specific sections (includes debug configuration)
- `build_radiant_config.json` - Configuration file for the radiant sub-project

### Dependency Setup Scripts
- `setup-mac-deps.sh` - Mac native dependency setup script
- `setup-linux-deps.sh` - Linux (Ubuntu) native dependency setup script
- `setup-windows-deps.sh` - Windows cross-compilation dependency setup script

#### Dependencies Installed

- **tree-sitter**: Incremental parsing library
- **tree-sitter-lambda**: Lambda language parser
- **GMP**: GNU Multiple Precision arithmetic library (via `libgmp-dev` package)
- **lexbor**: Fast HTML/XML parsing library (built from source)
- **MIR**: Lightweight JIT compiler infrastructure (built from source)
- **zlog**: High-performance logging library (built from source, optional)

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

### Native Compilation

After dependencies are set up, compile the project with enhanced options:

**Standard incremental build:**
```bash
./compile.sh
```

**Using Makefile (recommended):**
```bash
make build          # Incremental build
make rebuild        # Force full rebuild
make debug          # Debug build
make clean          # Clean build artifacts
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

### Native Compilation

After dependencies are set up, compile the project with enhanced options:

**Standard incremental build:**
```bash
./compile.sh
```

**Using Makefile (recommended):**
```bash
make build          # Incremental build
make rebuild        # Force full rebuild
make debug          # Debug build
make clean          # Clean build artifacts
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

Compile for Windows with enhanced options:

**Standard cross-compilation:**
```bash
./compile.sh --platform=windows
```

**Using Makefile:**
```bash
make cross-compile     # Cross-compile for Windows
make build-windows     # Same as above
```

## Usage

### Modern Build System

The build system now includes both a powerful shell script and a standard Makefile interface:

**Makefile targets (recommended):**
```bash
make help           # Show all available targets
make build          # Incremental build (default)
make debug          # Debug build
make release        # Release build
make rebuild        # Force complete rebuild
make clean          # Clean build artifacts
make distclean      # Complete cleanup
make test           # Run tests
make run            # Build and run
make cross-compile  # Cross-compile for Windows
```

**Advanced Makefile usage:**
```bash
make build JOBS=4          # Build with 4 parallel jobs
make info                  # Show project information
make time-build           # Benchmark build performance
make format               # Format source code
make lint                 # Run static analysis
```

### Enhanced compile.sh Options

The shell script now supports advanced features:

**Performance options:**
```bash
./compile.sh --jobs=N         # Specify parallel jobs (auto-detects by default)
./compile.sh --force          # Force complete rebuild
./compile.sh --debug          # Debug build with AddressSanitizer
./compile.sh -j 8 --force     # Maximum parallel rebuild
```

**Configuration options:**
```bash
./compile.sh config_file.json              # Use custom config
./compile.sh --platform=windows            # Cross-compile
./compile.sh --platform=debug              # Debug platform
./compile.sh config.json --platform=linux  # Platform with custom config
```

**Get help:**
```bash
./compile.sh --help          # Show detailed usage information
```

### Building Lambda Project

**Using Makefile (recommended):**
```bash
make lambda              # Build lambda project
make                     # Default lambda build
make debug               # Debug lambda build
make rebuild             # Force rebuild lambda
```

**Using compile.sh directly:**

**Linux Native Compilation (Ubuntu):**
```bash
./compile.sh                    # Incremental build
./compile.sh --debug            # Debug build with AddressSanitizer
./compile.sh --jobs=4           # Parallel build
./compile.sh --force            # Complete rebuild
```

**Mac Native Compilation (macOS):**
```bash
./compile.sh                    # Incremental build
./compile.sh --debug            # Debug build with AddressSanitizer
./compile.sh --jobs=8           # Maximum parallel build
./compile.sh --force --jobs=6   # Force rebuild with 6 jobs
```

**Cross-compilation for Windows:**
```bash
./compile.sh --platform=windows            # Incremental cross-compile
./compile.sh --platform=windows --jobs=4   # Parallel cross-compile
./compile.sh --platform=windows --force    # Force cross-compile rebuild
```

### Building Radiant Project

**Using Makefile:**
```bash
make radiant            # Build radiant project
make window             # Alias for radiant
make all                # Build all projects (lambda + radiant)
```

**Using compile.sh directly:**

**Mac Native Compilation (macOS):**
```bash
./compile.sh build_radiant_config.json                    # Standard build
./compile.sh build_radiant_config.json --jobs=4           # Parallel build
./compile.sh build_radiant_config.json --force            # Force rebuild
```

### Development Workflow

**Quick development cycle:**
```bash
# Standard development workflow
make                    # Quick incremental build
make test              # Run tests
make run               # Build and run

# When debugging issues
make clean             # Clean build artifacts
make debug             # Debug build with symbols
make rebuild           # Force complete rebuild

# Code quality
make lint              # Run static analysis
```

**Performance testing:**
```bash
make time-build        # Benchmark build performance
make benchmark         # Multiple timed builds
make parallel          # Maximum parallel build test
```

### Help
```bash
./compile.sh --help
make help                # Show all Makefile targets
```

## Advanced Features

### Incremental Compilation

The build system now includes intelligent incremental compilation that significantly speeds up development:

**How it works:**
- Only recompiles source files that have changed since last build
- Checks header file dependencies automatically
- Compares file modification timestamps efficiently
- Uses cached header scanning for performance

**Benefits:**
- 5-10x faster builds when only few files changed
- 50-100x faster when only headers changed
- Automatic dependency tracking
- No manual dependency management required

**Force full rebuild when needed:**
```bash
./compile.sh --force       # Via script
make rebuild               # Via Makefile
```

### Parallel Compilation

Automatic parallel compilation utilizes multiple CPU cores:

**Automatic detection:**
- Auto-detects available CPU cores (macOS/Linux)
- Limits to maximum of 8 jobs for stability
- Optimizes job scheduling to prevent system overload

**Manual control:**
```bash
./compile.sh --jobs=4      # Use 4 parallel jobs
./compile.sh -j 8          # Use 8 parallel jobs
make build JOBS=6          # Use 6 jobs via Makefile
```

**Performance gains:**
- 2-4x faster compilation on multi-core systems
- Scales automatically with available hardware
- Smart job limiting prevents thrashing

### Build Performance Optimization

**Header file caching:**
- Single scan of include directories per build
- Cached timestamp comparison
- Eliminates redundant filesystem operations

**Optimized file checking:**
- Batch timestamp operations
- Reduced string processing overhead
- Minimized repeated filesystem calls

**Parallel processing:**
- Background compilation processes
- Efficient job queue management
- Result collection and error handling

### Cross-Platform Support

**Enhanced platform detection:**
- Automatic compiler selection (clang/gcc/mingw)
- Platform-specific optimization flags
- Library path detection and configuration

**Supported platforms:**
- macOS (native with clang)
- Linux (native with gcc/clang)
- Windows (cross-compilation with MinGW-w64)

### Build System Integration

**Makefile features:**
- Standard targets (build, clean, test, install)
- Automatic job detection
- Integration with compile.sh features
- Development workflow optimization

**Configuration system:**
- JSON-based configuration files
- Platform-specific overrides
- Library and dependency management
- Flexible flag and option handling

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
   # Using Makefile (recommended)
   make clean && make rebuild
   make distclean && make      # Complete cleanup
   
   # Using setup scripts
   # For Linux
   ./setup-linux-deps.sh clean
   ./setup-linux-deps.sh
   
   # For Mac
   ./setup-mac-deps.sh clean
   ./setup-mac-deps.sh
   ```

2. **Force complete rebuild**:
   ```bash
   ./compile.sh --force        # Force rebuild
   make rebuild                # Makefile equivalent
   ```

3. **Check Dependencies**: The setup scripts provide detailed status reports

4. **Debug build issues**:
   ```bash
   make debug                  # Build with debug symbols
   make check                  # Run static analysis
   ./compile.sh --help         # Show all options
   ```

### Performance Issues

1. **Slow incremental builds**:
   ```bash
   # Check what will be built
   make what-will-build
   
   # Benchmark build performance
   make time-build
   
   # Try parallel compilation
   ./compile.sh --jobs=4
   ```

2. **Memory issues during parallel compilation**:
   ```bash
   # Reduce parallel jobs
   ./compile.sh --jobs=2
   make build JOBS=2
   ```

### Cross-compilation Issues

1. **MinGW-w64 not found**:
   ```bash
   # On macOS
   brew install mingw-w64
   
   # On Ubuntu
   sudo apt install mingw-w64
   ```

2. **Windows dependencies missing**:
   ```bash
   ./setup-windows-deps.sh     # Rebuild Windows dependencies
   ```

## Advanced Usage

### Build Configurations

The system supports multiple build configurations:

**Debug vs Release Builds:**
```bash
make debug                  # Debug build with AddressSanitizer
make release                # Optimized release build
./compile.sh --debug        # Debug build via script
./compile.sh --platform=debug  # Explicit debug platform
```

**Configuration files:**
```bash
./compile.sh build_lambda_config.json          # Lambda project
./compile.sh build_radiant_config.json         # Radiant project  
./compile.sh --debug                            # Debug lambda build
./compile.sh --platform=debug                  # Explicit debug platform
```

**Integrated debug configuration:**
The debug configuration is now integrated into the main `build_lambda_config.json` file as a platform variant. 
It includes AddressSanitizer for memory debugging and uses a separate build directory (`build_debug`).

**Custom configurations:**
The configuration supports platform-specific settings including debug mode:

```json
{
  "platforms": {
    "debug": {
      "output": "lambda_debug.exe",
      "flags": [
        "fms-extensions",
        "pedantic", 
        "fcolor-diagnostics",
        "fsanitize=address",
        "fno-omit-frame-pointer",
        "O1"
      ],
      "linker_flags": ["fsanitize=address"],
      "build_dir": "build_debug",
      "debug": true
    }
  }
}
```

### Performance Monitoring

**Build timing:**
```bash
make time-build             # Time a single build
make benchmark              # Multiple timed builds
time make rebuild           # Time via shell
```

**Build information:**
```bash
make info                   # Show project information
make what-will-build        # Preview compilation needs
./compile.sh --help         # Show all script options
```

### Development Tools Integration

**Static analysis:**
```bash
make lint                   # Run cppcheck
```

**Testing:**
```bash
make test                   # Run available tests
make run                    # Build and run executable
```

### Advanced Compilation Options

**Compiler selection:**
- Automatic detection: clang (macOS), gcc (Linux), mingw (Windows cross-compile)
- Override via configuration files
- Platform-specific optimization flags

**Library management:**
- Static and dynamic library support
- Platform-specific library paths
- Automatic dependency resolution

**Memory and performance:**
- Optimized incremental builds
- Parallel compilation with job limiting
- Cached header dependency checking

### Continuous Integration

The build system is designed for CI/CD environments:

**Automated builds:**
```bash
make build                  # Standard incremental build
make rebuild                # Force complete rebuild
make test                   # Run test suite
make clean                  # Clean for fresh build
```

**Cross-platform CI:**
```bash
# Linux CI
make build

# macOS CI  
make build

# Windows cross-compile CI
make cross-compile
```

**Build verification:**
```bash
make info                   # Build system information
```

## Build System Analysis and Comparison

This section provides a comprehensive analysis of our custom `compile.sh` build system compared to industry-standard build tools like CMake, Autotools, Meson, and Bazel.

### Our Custom Build System Features

**Strengths:**
1. **JSON-based configuration** - Clean, readable configuration files
2. **Cross-compilation support** - Built-in Windows cross-compilation via MinGW
3. **Incremental compilation** - Smart dependency checking with header file caching
4. **Parallel compilation** - Multi-threaded builds with automatic CPU detection
5. **Platform-specific overrides** - Different settings per target platform
6. **Rich diagnostics** - Clickable error links, colored output, build summaries
7. **Flexible flags** - Easy to add/modify compiler and linker flags
8. **Force rebuild option** - Clean builds when needed

### Comparison with Major Build Systems

#### 1. **Make/Makefile**
**Advantages of our script:**
- More readable JSON vs cryptic Makefile syntax
- Better cross-platform support out of the box
- Colored output and modern terminal features
- Automatic parallel job detection

**Advantages of Make:**
- Universal availability on Unix systems
- Pattern rules for scalability
- Mature dependency tracking
- Integration with autotools ecosystem

#### 2. **CMake**
**Advantages of CMake:**
- **Scalability**: Handles very large projects (thousands of files)
- **IDE integration**: Generates project files for Visual Studio, Xcode, etc.
- **Package management**: Built-in find modules for libraries
- **Testing framework**: CTest integration
- **Installation support**: CPack for packaging
- **Cross-platform**: Works on Windows, macOS, Linux natively

**Advantages of our script:**
- **Simplicity**: No learning curve for CMake's complex syntax
- **Transparency**: Clear shell commands vs generated makefiles
- **JSON config**: More readable than CMakeLists.txt

#### 3. **Autotools (Autoconf/Automake)**
**Advantages of Autotools:**
- **Portability**: Handles diverse Unix variants automatically
- **Configuration**: Automatic library detection and feature checking
- **Standards compliance**: Follows GNU coding standards
- **Mature**: Decades of development

**Advantages of our script:**
- **Modern approach**: No need for `./configure` dance
- **Faster setup**: Immediate compilation without configuration step
- **Cleaner**: No generated files cluttering the source tree

#### 4. **Meson**
**Advantages of Meson:**
- **Speed**: Very fast builds and configuration
- **Python-based**: More approachable syntax
- **Cross-compilation**: Excellent cross-compilation support
- **IDE support**: Good integration with modern IDEs

**Advantages of our script:**
- **No dependencies**: Pure shell script
- **JSON config**: Familiar format for most developers
- **Customizable**: Easy to modify behavior

#### 5. **Bazel**
**Advantages of Bazel:**
- **Massive scale**: Handles Google-sized codebases
- **Reproducible builds**: Hermetic builds with precise dependencies
- **Remote caching**: Distributed build caching
- **Multi-language**: Java, C++, Python, etc. in one build

**Advantages of our script:**
- **Simplicity**: No complex BUILD files
- **Low overhead**: Minimal setup for small-medium projects

### When Our Script Excels

Our script is particularly well-suited for:

1. **Small to medium projects** (< 1000 source files)
2. **Cross-compilation** scenarios (especially to Windows)
3. **Rapid prototyping** where build system setup time matters
4. **Teams familiar with shell scripting** but not build system DSLs
5. **Projects with simple dependency chains**
6. **Development workflows** requiring frequent clean builds

### Limitations Compared to Industrial Tools

1. **Scalability**: Manual file listing doesn't scale to thousands of files
2. **Dependency tracking**: No automatic header dependency generation
3. **Library management**: No built-in package manager integration
4. **IDE integration**: No project file generation for IDEs
5. **Testing integration**: No built-in test framework support
6. **Installation**: No standardized install/packaging mechanism

### Recommendations for Future Improvements

To make our script more competitive with industrial tools:

1. **Library detection**: Add automatic library finding (pkg-config integration)
2. **Modular config**: Support importing/including config files
3. **Tool integration**: Add hooks for formatters, linters, static analyzers

### Performance Characteristics

**Build Speed:**
- Parallel compilation with job limiting (max 8 jobs)
- Incremental builds with header file caching
- Smart linking decisions based on timestamp comparison

**Memory Usage:**
- Efficient header cache initialization
- Batch processing of file operations
- Minimal memory footprint for build tracking

**Scalability Limits:**
- Optimal for projects with < 500 source files
- JSON parsing overhead becomes noticeable with > 1000 files
- Manual file enumeration requires maintenance

### Verdict

Our script represents a **pragmatic middle ground** between simple Makefiles and complex build systems like CMake. It's well-engineered for its target use case and includes several modern features (parallel builds, incremental compilation, cross-compilation) that many traditional build systems handle poorly or require significant setup.

**Key Strengths:**
- **Transparency**: You can see exactly what commands are being run
- **Hackability**: Easy to modify and extend for specific needs
- **Modern UX**: Colored output, clickable links, informative summaries
- **Cross-compilation**: Excellent Windows cross-compile support

**Best Use Case:** Projects that need more sophistication than a simple Makefile but want to avoid the complexity and learning curve of CMake or Autotools.

For our current project size (~40 source files) and requirements (cross-compilation, incremental builds, modern developer experience), this custom build system is actually quite competitive with industrial tools while maintaining much better transparency and hackability.


