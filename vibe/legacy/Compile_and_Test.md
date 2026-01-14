# Lambda Build System: Compilation and Testing Documentation

## Overview

The Lambda project uses a unified, incremental build system designed for efficient compilation and robust error reporting. The system supports both C and C++ source files with intelligent dependency tracking, parallel compilation, and comprehensive error diagnostics.

## Architecture and Design

### Core Components

1. **Main Build Script** (`compile.sh`)
   - Primary entry point for building the Lambda executable
   - Orchestrates compilation and linking phases
   - Integrates with centralized build functions

2. **Core Build Library** (`utils/build_core.sh`)
   - Centralized build logic shared across all build scripts
   - Contains reusable functions for compilation, linking, and error reporting
   - Implements dependency tracking and incremental build logic

3. **Build Utilities Library** (`utils/build_utils.sh`)
   - Shared utility functions for configuration parsing and library resolution
   - JSON configuration handling with jq and fallback parsing
   - Library dependency resolution and test configuration management

4. **Test Build System** (`test/test_build.sh`)
   - Enhanced test compilation system leveraging main build infrastructure
   - Unified configuration with test suite-specific overrides
   - Automatic dependency resolution for test executables

5. **Test Execution System** (`test/test_run.sh`)
   - Comprehensive test suite runner with category-based organization
   - Parallel and sequential execution modes
   - Test result formatting and breakdown by suite

6. **Makefile Integration**
   - Unified build orchestration with standard make targets
   - Automatic parallel job detection and tree-sitter dependency management
   - Comprehensive test target definitions

7. **Configuration System**
   - JSON-based build configuration (`build_lambda_config.json`)
   - Environment variable overrides
   - Cross-compilation support

## File Structure

```
lambda/
├── compile.sh                    # Main build script
├── utils/
│   ├── build_core.sh            # Core build functions library
│   └── build_utils.sh           # Shared utility functions library
├── test/
│   ├── test_build.sh            # Test compilation system
│   └── test_run.sh              # Test execution runner
├── Makefile                     # Build orchestration and targets
├── build_lambda_config.json     # Build configuration
├── build/                       # Build output directory
│   ├── *.o                      # Object files
│   ├── *.d                      # Dependency files
│   └── lambda.exe               # Final executable
├── lambda/                      # Core source files
├── lib/                         # Library source files
├── radiant/                     # Radiant module source files
└── typeset/                     # Typeset module source files
```

## Configuration Management

### JSON Configuration (`build_lambda_config.json`)

The build system uses a JSON configuration file that defines:

- **Source directories and file patterns**
- **Compiler settings** (C/C++ compilers, flags, warnings)
- **Include paths and library dependencies**
- **Output paths and executable names**
- **Cross-compilation settings**
- **Parallel compilation limits**

### Environment Variable Overrides

Key environment variables that can override configuration:

- `CC` - C compiler (default: clang)
- `CXX` - C++ compiler (default: clang++)
- `FORCE_REBUILD` - Force complete rebuild
- `PARALLEL_JOBS` - Number of parallel compilation jobs
- `CROSS_COMPILE` - Enable cross-compilation mode

## Build Utilities Library (`utils/build_utils.sh`)

The `build_utils.sh` library provides essential utility functions used across the build system:

### Key Functions

**Configuration Management:**
- `get_json_value()` - Extract values from JSON config with platform-specific overrides
- `get_json_array()` - Extract array values from JSON configuration
- `validate_config_file()` - Validate JSON configuration file integrity
- `has_jq_support()` - Check for jq availability with fallback parsing

**Library Dependency Resolution:**
- `resolve_library_dependencies()` - Main function to resolve library dependencies
- `resolve_single_library_dependency()` - Resolve individual library with deduplication
- `resolve_library_legacy()` - Fallback for hardcoded library definitions
- `get_automatic_test_dependencies()` - Auto-detect test dependencies by filename

**Build Integration:**
- `validate_build_objects()` - Ensure main build objects are up-to-date
- `get_library_object_files()` - Map library names to required object files
- `get_minimal_object_set()` - Get minimal object set for test builds
- `check_build_prerequisites()` - Validate build prerequisites for testing

**Source File Management:**
- `expand_source_patterns()` - Expand glob patterns to actual source files
- `collect_library_sources()` - Collect sources from library definitions
- `map_sources_to_objects()` - Map source files to corresponding object files

**Unified Build Functions:**
- `unified_compile_sources()` - Compile sources with library dependencies
- `unified_link_objects()` - Link objects with library dependencies

### Library Configuration Support

The build utilities support both JSON-based library configuration and legacy hardcoded definitions:

**JSON Library Definition Format:**
```json
{
  "name": "library-name",
  "include": "path/to/includes",
  "source_files": ["explicit/source1.c", "explicit/source2.c"],
  "source_patterns": ["pattern/*.c", "another/*.cpp"],
  "objects": ["legacy/object1.o"],
  "lib": "path/to/library.a",
  "link": "static|dynamic|inline",
  "libraries": ["nested-dependency"],
  "special_flags": "-custom-flag"
}
```

**Legacy Hardcoded Libraries:**
- `strbuf`, `strview`, `mem-pool`, `num_stack`, `datetime`, `string`
- `mime-detect`, `criterion`, `lambda-runtime-full`

## Test Build System (`test/test_build.sh`)

The test build system provides enhanced compilation capabilities specifically designed for test executables:

### Key Features

**Unified Configuration Integration:**
- Leverages main build infrastructure through `build_core.sh` and `build_utils.sh`
- Inherits configuration from `build_lambda_config.json` with test-specific overrides
- Supports test suite-specific configuration values

**Test Configuration Management:**
- `get_config()` - Enhanced configuration retrieval with test suite inheritance
- Automatic fallback from test-specific to global configuration values
- Support for test suite categories (library, input, mir, lambda, validator)

**Build Prerequisites:**
- Ensures main build objects are up-to-date before test compilation
- Validates essential object files and dependencies
- Integrates with Makefile dependency chain (`build-test` depends on `build`)

**Test Compilation Process:**
- Reuses pre-compiled main build objects for efficiency
- Compiles only test-specific source files
- Automatic library dependency resolution for test executables

### Usage Examples

```bash
# Build all test executables
./test/test_build.sh all

# Build specific test suite
./test/test_build.sh library

# Build with parallel jobs
PARALLEL_JOBS=4 ./test/test_build.sh all
```

## Test Execution System (`test/test_run.sh`)

The test execution system provides comprehensive test running capabilities with advanced organization and reporting:

### Key Features

**Test Suite Organization:**
- Groups tests by categories from `build_lambda_config.json`
- Supports targeted execution by test suite (library, input, mir, lambda, validator)
- Automatic test discovery and categorization

**Execution Modes:**
- **Parallel execution** (default) - Runs tests concurrently for speed
- **Sequential execution** - Runs tests one at a time for debugging
- **Raw output mode** - Shows unformatted test output
- **Formatted output** - Enhanced test result presentation

**Command Line Interface:**
```bash
# Run all tests in parallel (default)
./test/test_run.sh

# Run specific test suite
./test/test_run.sh --target=library

# Run tests sequentially
./test/test_run.sh --sequential

# Show raw output without formatting
./test/test_run.sh --raw
```

**Test Result Processing:**
- Automatic compilation of test executables before execution
- Test result aggregation and formatting
- Error reporting and failure analysis

### Supported Test Suites

- **library** - Core library functionality tests
- **input** - Input processing and MIME detection tests
- **mir** - MIR JIT compilation tests
- **lambda** - Lambda runtime and evaluation tests
- **validator** - Schema validation and type checking tests

## Makefile Integration

The Makefile provides a unified interface for all build and test operations:

### Build Targets

**Primary Build Targets:**
- `make build` - Incremental build with Unicode support (default)
- `make debug` - Debug build with symbols and AddressSanitizer
- `make release` - Optimized release build
- `make rebuild` - Force complete rebuild
- `make cross-compile` - Cross-compile for Windows

**Project-Specific Targets:**
- `make lambda` - Build lambda project specifically
- `make radiant` - Build radiant project
- `make window` - Build window project
- `make all` - Build all projects

### Test Targets

**Comprehensive Test Execution:**
- `make test` - Run comprehensive test suite
- `make test-parallel` - Run tests in parallel mode
- `make build-test` - Build all test executables

**Test Suite Categories:**
- `make test-library` - Run library tests only
- `make test-input` - Run input processing tests
- `make test-mir` - Run MIR JIT tests
- `make test-lambda` - Run lambda runtime tests
- `make test-validator` - Run validator tests
- `make test-std` - Run Lambda Standard Tests

**Advanced Test Modes:**
- `make test-coverage` - Run tests with code coverage analysis
- `make test-memory` - Run memory leak detection tests
- `make test-benchmark` - Run performance benchmark tests
- `make test-fuzz` - Run fuzzing tests for robustness
- `make test-integration` - Run end-to-end integration tests

### Maintenance Targets

**Cleanup Operations:**
- `make clean` - Remove build artifacts (object files, dependency files)
- `make clean-test` - Remove test outputs and temporary files
- `make clean-grammar` - Remove generated grammar files
- `make clean-all` - Remove all build directories
- `make distclean` - Complete cleanup (build dirs + executables + tests)

**Grammar and Parser Management:**
- `make generate-grammar` - Generate parser and ts-enum.h from grammar.js
- `make tree-sitter-libs` - Build tree-sitter and tree-sitter-lambda libraries

### Automatic Features

**Parallel Job Detection:**
- Automatically detects CPU core count for optimal parallel compilation
- Limits parallel jobs to reasonable maximum (8 jobs)
- Supports manual override with `JOBS=N` parameter

**Tree-sitter Dependency Management:**
- Automatic parser regeneration when `grammar.js` changes
- Dependency chain: `grammar.js` → `parser.c` → `ts-enum.h` → source files
- Automatic library building for tree-sitter dependencies

**Configuration Support:**
- Default configuration: `build_lambda_config.json`
- Alternative configurations: `build_radiant_config.json`
- Environment variable integration

### Usage Examples

```bash
# Basic build with auto-detected parallel jobs
make build

# Build with specific job count
make build JOBS=4

# Debug build with sanitizers
make debug

# Run comprehensive test suite
make test

# Run specific test category
make test-library

# Force complete rebuild
make rebuild

# Cross-compile for Windows
make cross-compile

# Complete cleanup
make distclean
```

## Implemented Features

### 1. Incremental Build System

**Smart Dependency Tracking:**
- Automatic `.d` file generation using `-MMD -MP` compiler flags
- Precise dependency checking based on file timestamps
- Header cache fallback for missing dependency files
- Intelligent recompilation decisions

**Functions:**
- `needs_recompilation()` - Determines if source file needs recompilation
- `needs_linking()` - Determines if linking is required

### 2. Parallel Compilation

**Capabilities:**
- Automatic detection of optimal job count based on CPU cores
- Background process management for parallel compilation
- Job limiting to prevent system overload
- Result collection and error aggregation

**Implementation:**
- Uses background processes with PID tracking
- Temporary files for status and output collection
- Sequential fallback for single-file builds

### 3. Unified Compilation and Linking

**Compilation Phase:**
- `build_compile_sources()` - Main compilation orchestrator
- `compile_single_file()` - Individual file compilation
- Support for mixed C/C++ projects
- Automatic compiler selection based on file extensions

**Linking Phase:**
- `build_link_objects()` - Unified linking function
- Automatic linker selection (clang vs clang++)
- Library dependency resolution
- Cross-compilation support

### 4. Advanced Error Reporting

**Error Detection:**
- Real-time compilation error parsing
- Error count tracking (errors, warnings, notes)
- Fail-fast behavior on compilation errors

**Error Formatting:**
- `format_compilation_diagnostics()` - Centralized error formatting
- Clickable file links with line/column numbers
- Color-coded error messages
- Relative path conversion for cleaner output

**Build Status Reporting:**
- `display_build_status()` - Comprehensive build summary
- Dependency tracking statistics
- Cross-compilation file type information
- Success/failure status with exit codes

### 5. Dependency Management

**Automatic Dependency Generation:**
- Compiler-generated `.d` files for precise tracking
- Header file change detection
- Stale dependency cleanup on compilation failure

**Fallback Mechanisms:**
- Header cache timestamp comparison
- Graceful handling of missing dependency files
- Force rebuild capability

### 6. Cross-Platform Support

**Compiler Detection:**
- Automatic C/C++ compiler selection
- Platform-specific stat command handling
- Library path resolution

**Build Modes:**
- Debug and release configurations
- Cross-compilation support
- Platform-specific optimizations

## Build Process Flow

### 1. Initialization Phase
```bash
# Load configuration
# Set up environment variables
# Initialize dependency tracking
# Validate source files and directories
```

### 2. Compilation Phase
```bash
# Determine files needing recompilation
# Set up parallel compilation jobs
# Execute compilation with error capture
# Collect and format compilation results
```

### 3. Error Analysis Phase
```bash
# Parse compilation output for errors/warnings
# Format diagnostic messages with clickable links
# Determine build success/failure status
```

### 4. Linking Phase (if compilation successful)
```bash
# Check if linking is needed
# Determine appropriate linker
# Execute linking with library resolution
# Report linking results
```

### 5. Reporting Phase
```bash
# Display comprehensive build summary
# Show dependency tracking statistics
# Report final build status
# Exit with appropriate code
```

## Key Design Principles

### 1. **Modularity**
- Centralized build logic in `build_core.sh`
- Reusable functions across different build scripts
- Clean separation of concerns

### 2. **Robustness**
- Comprehensive error handling and reporting
- Fail-fast behavior on compilation errors
- Graceful fallback mechanisms

### 3. **Performance**
- Intelligent incremental builds
- Parallel compilation support
- Efficient dependency tracking

### 4. **Developer Experience**
- Clickable error links for IDE integration
- Clear, color-coded diagnostic messages
- Comprehensive build status reporting

### 5. **Maintainability**
- JSON-based configuration
- Environment variable overrides
- Consistent coding patterns and documentation

## Usage Examples

### Basic Build
```bash
./compile.sh
```

### Force Rebuild
```bash
FORCE_REBUILD=true ./compile.sh
```

### Parallel Build with Custom Job Count
```bash
PARALLEL_JOBS=4 ./compile.sh
```

### Cross-Compilation
```bash
CROSS_COMPILE=true CC=arm-linux-gnueabihf-gcc ./compile.sh
```

## Testing Integration

The build system is designed to support comprehensive testing workflows:

### Test Build Scripts
- Shared build logic through `build_core.sh`
- Consistent error reporting across test and main builds
- Support for test-specific configurations

### Test Execution
- Integration with testing frameworks (Criterion)
- Test result reporting and aggregation
- Continuous integration support

## Future Enhancements

### Planned Features
- Automated test discovery and execution
- Build caching and artifact management
- Integration with package managers
- Enhanced cross-compilation support
- Build performance profiling

### Extensibility
- Plugin architecture for custom build steps
- Configurable build pipelines
- Integration with external tools and IDEs

## Troubleshooting

### Common Issues

1. **Compilation Errors Not Detected**
   - Check error parsing in `format_compilation_diagnostics()`
   - Verify compiler output format compatibility

2. **Incremental Build Not Working**
   - Check `.d` file generation and parsing
   - Verify timestamp comparison logic in `needs_recompilation()`

3. **Parallel Compilation Issues**
   - Adjust `PARALLEL_JOBS` environment variable
   - Check system resource limits

4. **Linking Failures**
   - Verify library paths and dependencies
   - Check object file existence after compilation

### Debug Mode
Enable verbose output by modifying debug flags in build scripts for detailed troubleshooting information.

---

*This documentation reflects the current state of the Lambda build system as of the latest implementation. For the most up-to-date information, refer to the source code and configuration files.*
