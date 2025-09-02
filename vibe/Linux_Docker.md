# Lambda Linux Docker Setup and Build Verification

## Overview

This document outlines the approach for setting up and testing the Lambda engine build system on Linux using Docker containers. The primary goal is to verify that Lambda can be successfully compiled and tested on Linux platforms using a containerized environment.

## Current Status (Updated 2025-09-02)

### **Successfully Completed:**
- Docker container setup with volume mounting for live source synchronization
- Complete dependency installation (clang, cmake, pkg-config, python3)
- Essential development libraries installed (mpdecimal, utf8proc, libedit, libcurl)
- MIR library compilation and installation
- GMP library installation via apt
- Tree-sitter and Tree-sitter-lambda compilation
- Build system configuration for native Linux compilation
- Cross-compilation flags properly configured (`cross_compile: false`)

## New Volume Mounting Approach

### Key Changes

1. **Volume Mounting**: Mount the entire Lambda repository as a shared volume in the Docker container
2. **Live Synchronization**: Changes to source files are immediately available in container
3. **Simplified Script**: No file copying logic needed
4. **Faster Iteration**: Instant sync for development and testing cycles

### Docker Volume Configuration

```bash
# Mount Lambda repo as read-write volume
docker run --rm -it \
    -v "$(pwd):/workspace/lambda" \
    -w "/workspace/lambda" \
    lambda-setup-test bash
```

## Testing Scope

### 1. Dependency Setup Testing
- **Objective**: Verify `setup-linux-deps.sh` works correctly in clean Ubuntu environment
- **Test Steps**:
  - Run `./setup-linux-deps.sh` in container
  - Verify all dependencies are installed correctly
  - Check library availability and versions
  - Fix any missing packages or build failures
- **Success Criteria**: All dependencies built/installed without errors

### 2. Lambda Compilation Testing  
- **Objective**: Verify `compile.sh` and `make` can build Lambda.exe on Linux
- **Test Steps**:
  - Run `./compile.sh` to build Lambda.exe
  - Run `make` to build using Makefile
  - Verify successful compilation
  - Test basic executable functionality
- **Success Criteria**: Lambda.exe builds successfully on Linux (cross-platform compilation verified)

### 3. Test Suite Compilation and Execution
- **Objective**: Verify `make test` compiles and runs all Lambda test executables
- **Test Steps**:
  - Run `make test` or `make build-test` to compile test executables
  - Execute test suites (roundtrip, math, validation, etc.)
  - Verify test executables run (pass/fail results not critical)
- **Success Criteria**: All test executables compile and can be executed on Linux

## Cross-Platform Compilation Goals

### Primary Objective
**Ensure Lambda engine code and test code can compile and run on Linux**

### Key Clarifications
- **Compilation Focus**: The goal is successful compilation, not test pass rates
- **Cross-Platform Support**: Lambda is designed for cross-platform compatibility
- **Minor Fixes Expected**: Some platform-specific adjustments may be needed
- **Test Execution**: Tests should run (whether they pass or fail is secondary)

### Success Criteria
- ✅ All dependencies install successfully on Ubuntu
- ✅ Lambda.exe compiles without errors on Linux
- ✅ All test executables compile without errors on Linux  
- ✅ Test executables can be launched and executed
- ✅ Any platform-specific compilation issues identified and fixed

## Implementation Plan

### Phase 1: Docker Environment Setup
- Create Ubuntu 22.04 base image with essential tools
- Configure non-root user with sudo privileges
- Set up proper working directory structure

### Phase 2: Volume Mounting Integration
- Replace file copying with volume mounting
- Configure proper file permissions for shared volume
- Ensure container can read/write to mounted Lambda directory

### Phase 3: Testing Workflow
- Implement dependency setup testing
- Add compilation verification
- Include test suite execution
- Add comprehensive status reporting

### Phase 4: Validation and Documentation
- Test the complete workflow
- Document usage instructions
- Add troubleshooting guide

## Docker Container Specifications

### Base Image
- **OS**: Ubuntu 22.04 LTS
- **Architecture**: x86_64 (with potential ARM64 support)

### Pre-installed Tools
- `sudo`, `git`, `curl`, `wget`
- `build-essential` (gcc, g++, make)
- Basic development utilities

### User Configuration
- Non-root user: `testuser`
- Sudo privileges without password
- Working directory: `/workspace/lambda` (mounted volume)

### Environment Variables
- `DEBIAN_FRONTEND=noninteractive`
- `TZ=UTC`
- Standard build environment variables

## Testing Workflow

### 1. Container Startup
```bash
./setup-linux-docker.sh
```

### 2. Dependency Setup Phase
```bash
# Inside container
./setup-linux-deps.sh
```

### 3. Compilation Phase  
```bash
# Inside container
./compile.sh
```

### 4. Test Execution Phase
```bash
# Inside container
make test
```

### 5. Results Verification
- Dependency installation status
- Compilation success/failure
- Test execution results
- Performance metrics

## Expected Benefits

### Development Efficiency
- **Instant sync**: No copying delays for source changes
- **Faster iteration**: Immediate testing of code modifications
- **Reduced complexity**: Simpler script maintenance

### Testing Reliability
- **Complete environment**: Full Lambda source tree available
- **Consistent state**: No risk of missing files or directories
- **Reproducible results**: Same environment every time

### Maintenance Advantages
- **Self-updating**: New files automatically available
- **Simplified script**: Less code to maintain
- **Better debugging**: Direct access to all source files

## Success Metrics

### Functional Requirements
- ✅ `setup-linux-deps.sh` completes successfully
- ✅ All dependencies properly installed and verified
- ✅ `compile.sh` builds Lambda.exe without errors
- ✅ `make test` compiles and runs all test suites
- ✅ Volume mounting works correctly with proper permissions

### Performance Requirements
- ⚡ Container startup time < 30 seconds
- ⚡ Volume mounting overhead minimal
- ⚡ File synchronization instantaneous

### Reliability Requirements
- 🔄 Consistent results across multiple runs
- 🔄 Proper cleanup after container exit
- 🔄 Error handling for common failure scenarios

## Risk Mitigation

### File Permission Issues
- **Risk**: Volume mounting may cause permission conflicts
- **Mitigation**: Configure proper UID/GID mapping in container

### Path Dependencies
- **Risk**: Scripts may assume specific absolute paths
- **Mitigation**: Use relative paths and proper working directory setup

### Container Resource Limits
- **Risk**: Compilation may require significant resources
- **Mitigation**: Document minimum system requirements

## Future Enhancements

### Multi-Platform Support
- Add ARM64 architecture support
- Test on different Linux distributions
- Windows Docker Desktop compatibility

### CI/CD Integration
- GitHub Actions workflow integration
- Automated testing on pull requests
- Performance benchmarking

### Advanced Features
- Parallel compilation testing
- Memory usage monitoring
- Build artifact caching

## Conclusion

The volume mounting approach provides a more efficient, maintainable, and reliable solution for testing Lambda engine compilation in Docker containers. This plan ensures comprehensive testing coverage while improving development workflow efficiency.
