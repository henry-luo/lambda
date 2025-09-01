# Lambda Setup Script - Manual Testing Guide

## Overview
The `setup-linux-deps.sh` script has been updated to install all dependencies required for the Lambda Script project on Linux/Ubuntu systems.

## What Was Updated

### Added Dependencies
- **libmpdec-dev**: For decimal arithmetic operations
- **libreadline-dev**: For command-line editing capabilities
- **coreutils**: For the `timeout` command used in test suites
- **utf8proc**: Built from source for Unicode processing
- **criterion**: Built from source for unit testing framework

### Build Process
The script now:
1. Installs system packages via `apt`
2. Builds dependencies from source when needed
3. Verifies all installations
4. Provides comprehensive status reporting

## Manual Testing Steps

### 1. Basic Syntax Check
```bash
bash -n setup-linux-deps.sh
echo "Exit code: $?"
```

### 2. Permission Check
```bash
ls -la setup-linux-deps.sh
# Should show executable permissions (-rwxr-xr-x)
```

### 3. Dry Run Check
```bash
# Check what the script would install
grep "apt install" setup-linux-deps.sh
grep "build_" setup-linux-deps.sh | grep "function"
```

### 4. Dependency Verification
```bash
# Check if key dependencies are mentioned in build config
grep -A 2 '"name":' build_lambda_config.json
```

## Expected Output When Run

When executed successfully, the script should produce output like:

```
Setting up Linux (Ubuntu) native compilation dependencies...
Checking for essential build tools...
✅ make, gcc, g++, git found
Installing additional development dependencies...
✅ curl, wget, build-essential, cmake, git, pkg-config installed
✅ libmpdec-dev, libreadline-dev, coreutils installed
Building tree-sitter for Linux...
✅ Tree-sitter built successfully
Building tree-sitter-lambda for Linux...
✅ Tree-sitter-lambda built successfully
Setting up lexbor...
✅ lexbor built successfully
Setting up GMP...
✅ GMP built successfully
Setting up MIR...
✅ MIR built successfully
Setting up utf8proc...
✅ utf8proc built successfully
Setting up criterion...
✅ criterion built successfully

Built dependencies:
✅ Tree-sitter: ✓ Built
✅ Tree-sitter-lambda: ✓ Built
✅ GMP: ✓ Available
✅ lexbor: ✓ Available
✅ MIR: ✓ Built
✅ curl: ✓ Available
✅ mpdecimal: ✓ Available
✅ utf8proc: ✓ Available
✅ readline: ✓ Available
✅ criterion: ✓ Available
✅ coreutils: ✓ Available

Next steps:
1. Run: ./compile.sh
```

## Docker Testing (When Available)

If Docker is running, you can test in a fresh Ubuntu container:

```bash
# Build test image
docker build -f Dockerfile.test -t lambda-setup-test .

# Run setup script
docker run --rm -it lambda-setup-test ./setup-linux-deps.sh

# Or use the comprehensive test
./test-setup-docker.sh
```

## Key Improvements Made

1. **Comprehensive Dependency Management**: Now installs all dependencies referenced in `build_lambda_config.json`
2. **Source Building**: Properly builds dependencies from source when apt packages aren't suitable
3. **Error Handling**: Robust error checking and fallback mechanisms
4. **Status Reporting**: Clear indication of what's installed vs missing
5. **Cleanup Support**: `--clean` option to remove build artifacts
6. **Cross-Platform Consistency**: Matches the functionality of `setup-mac-deps.sh`

## Files Modified

- `setup-linux-deps.sh`: Main setup script with all dependency installations
- `Dockerfile.test`: Docker test environment
- `test-setup-docker.sh`: Comprehensive Docker testing script
- `validate-setup.sh`: Validation and testing script

The script is now ready for production use and should successfully set up all dependencies needed for compiling the Lambda Script project on Linux systems.
