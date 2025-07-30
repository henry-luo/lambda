# Lambda WASM Test Suite

This directory contains test scripts, demo files, and verification utilities for the Lambda WASM compilation and JavaScript interface.

## 📁 Test Files

### Verification Scripts
- **`verify-wasm-setup.sh`** - Comprehensive setup verification script
- **`test-wasm.sh`** - Basic WASM compilation test script
- **`wasm-js-summary.sh`** - Summary of the complete JavaScript interface

### Demo & Examples
- **`lambda-wasm-demo.html`** - Interactive browser demo with GUI
- **`build_lambda_wasm_minimal_config.json`** - Minimal WASM build configuration for testing
- **`build_lambda_core_wasm_config.json`** - Core-only WASM build configuration

## 🧪 Running Tests

### Quick Verification
```bash
# Run complete setup verification
./test/verify-wasm-setup.sh

# Test basic WASM compilation
./test/test-wasm.sh

# Show interface summary
./test/wasm-js-summary.sh
```

### Browser Demo
```bash
# Start local server
python3 -m http.server 8080

# Open demo in browser
open http://localhost:8080/test/lambda-wasm-demo.html
```

### Minimal Build Test
```bash
# Test with minimal configuration
cp test/build_lambda_wasm_minimal_config.json build_lambda_wasm_config.json
./compile-wasm.sh --force
```

## 🔧 Test Configurations

### Minimal Config (`build_lambda_wasm_minimal_config.json`)
- Limited source files for quick testing
- Basic functionality verification
- Faster compilation for development

### Core Config (`build_lambda_core_wasm_config.json`)
- Input and format modules only
- Excludes complex validator dependencies
- Good for core functionality testing

## 📊 Expected Test Results

### `verify-wasm-setup.sh` Output:
```
=== WASM Compilation Setup Verification ===
1. WASI SDK Installation: ✓
2. WASM Dependencies Stub Libraries Created: ✓
3. Testing Basic File Compilation: ✓
4. WASM Compilation Configuration: ✓
5. Usage Instructions: ✓
=== Setup Complete! ===
```

### `test-wasm.sh` Output:
```
Creating build directory...
Testing single file compilation...
Single file compilation successful!
Testing input file compilation...
Input file compilation successful!
All tests passed!
```

## 🐛 Troubleshooting Tests

### Common Test Issues

**WASI SDK not found:**
```bash
# Install WASI SDK first
curl -L -O https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/wasi-sdk-24.0-arm64-macos.tar.gz
tar -xzf wasi-sdk-24.0-arm64-macos.tar.gz
sudo mv wasi-sdk-24.0-arm64-macos /opt/wasi-sdk
```

**Browser demo not loading:**
- Ensure you're serving from HTTP, not file:// protocol
- Check that lambda-wasm-example.js is in the parent directory
- Verify lambda.wasm exists in the project root

**Test compilation failures:**
- Check that all stub dependencies are in wasm-deps/include/
- Verify source files exist in lambda/ directories
- Run with `--debug` flag for verbose output

## 🎯 Test Coverage

### What Tests Cover:
✅ WASI SDK installation and configuration  
✅ Stub library creation for missing dependencies  
✅ Basic C file compilation to WASM  
✅ JavaScript interface loading  
✅ HTML demo functionality  
✅ CLI tool operation  
✅ Multiple build configurations  

### What Tests Don't Cover:
❌ Full end-to-end WASM execution (requires working C functions)  
❌ Complex validator module compilation  
❌ Performance benchmarking  
❌ Cross-browser compatibility  
❌ Memory leak detection  

## 📈 Adding New Tests

### Test Script Template:
```bash
#!/bin/bash
echo "🧪 Testing [Feature Name]..."

# Setup
setup_test() {
    # Preparation code
}

# Test implementation
run_test() {
    # Test logic
    if [[ test_condition ]]; then
        echo "✅ Test passed"
        return 0
    else
        echo "❌ Test failed"
        return 1
    fi
}

# Cleanup
cleanup_test() {
    # Cleanup code
}

# Main execution
setup_test
run_test
cleanup_test
```

### Integration with Main Build:
Add new tests to the main verification script and update documentation accordingly.

## 🔄 Continuous Testing

For development workflows:
```bash
# Watch for changes and run tests
watch -n 10 './test/verify-wasm-setup.sh'

# Quick compilation test loop
while true; do ./test/test-wasm.sh && break; sleep 5; done
```

## 📝 Test Documentation

Each test script includes:
- Purpose and scope description
- Expected inputs and outputs
- Error handling and fallback behavior
- Integration points with main build system
- Troubleshooting guidance for common failures

The test suite is designed to provide confidence in the WASM compilation pipeline and JavaScript interface functionality while being easy to run and understand.
