# KLEE Symbolic Execution Integration for Lambda Script

This document provides a comprehensive guide for integrating and using KLEE symbolic execution to automatically discover runtime issues in the Lambda Script codebase.

## Overview

KLEE is a symbolic execution engine built on top of LLVM that can automatically generate test cases and find bugs such as:

- Division by zero
- Buffer overflows and out-of-bounds access
- Null pointer dereferences
- Assertion violations
- Arithmetic overflow/underflow
- Memory leaks and use-after-free

## Prerequisites

### Installing KLEE

#### macOS
```bash
# Install via Homebrew (if available)
brew install klee

# Or build from source
git clone https://github.com/klee/klee.git
cd klee
mkdir build && cd build
cmake ..
make
sudo make install
```

#### Linux (Ubuntu/Debian)
```bash
# Install dependencies
sudo apt-get install build-essential cmake curl file g++ git libcap-dev \
  libgoogle-perftools-dev libncurses5-dev libsqlite3-dev libtcmalloc-minimal4 \
  python3-pip unzip graphviz doxygen

# Install LLVM
sudo apt-get install clang-11 llvm-11 llvm-11-dev llvm-11-tools

# Build KLEE
git clone https://github.com/klee/klee.git
cd klee
mkdir build && cd build
cmake -DENABLE_SOLVER_STP=ON -DENABLE_POSIX_RUNTIME=ON ..
make
sudo make install
```

#### Verify Installation
```bash
which klee
which klee-clang
klee --version
```

## Project Integration

### 1. Test Harnesses

The project includes several KLEE test harnesses in `test/klee/`:

- `test_arithmetic.c` - Arithmetic operations and overflow detection
- `test_strings.c` - String operations and buffer overflow detection
- `test_arrays.c` - Array bounds checking and memory safety
- `test_memory_pool.c` - Memory pool allocation and management
- `test_validation.c` - Data validation and type checking

### 2. Build Integration

#### Makefile Targets

The project provides comprehensive Makefile integration:

```bash
# Full KLEE analysis pipeline
make klee-all

# Setup and compile tests
make klee-setup
make klee-compile

# Run specific tests
make klee-run-test_arithmetic
make klee-run-test_strings

# Analyze results
make klee-analyze

# Quick check with shorter timeout
make klee-check

# Custom timeout
make klee-custom TIMEOUT=600

# Interactive runner
make klee-interactive

# Clean artifacts
make klee-clean
```

#### Automation Script

Use the comprehensive analysis script:

```bash
./test/run_klee_analysis.sh
```

This script:
- Checks KLEE installation
- Compiles all test harnesses
- Runs KLEE with optimal settings
- Analyzes results and generates reports
- Provides colored output and progress tracking

## Usage Examples

### 1. Basic Analysis

```bash
# Run complete analysis
make klee-all
```

This will:
1. Setup KLEE directories
2. Compile all test harnesses to LLVM bitcode
3. Run KLEE symbolic execution
4. Generate comprehensive analysis report

### 2. Targeted Testing

```bash
# Test only arithmetic operations
make klee-run-test_arithmetic
make klee-analyze-test_arithmetic

# Test memory management
make klee-run-test_memory_pool
make klee-analyze-test_memory_pool
```

### 3. Custom Analysis

```bash
# Longer analysis with 10-minute timeout
make klee-custom TIMEOUT=600

# Quick CI check
make klee-check
```

### 4. Manual KLEE Usage

```bash
# Compile test to bitcode
klee-clang -I. -Ilambda -emit-llvm -c -g -O0 test/klee/test_arithmetic.c -o test_arithmetic.bc

# Run KLEE
klee --libc=uclibc --posix-runtime --write-test-cases test_arithmetic.bc

# Analyze results
ls klee-last/
ktest-tool klee-last/test000001.ktest
```

## Understanding Results

### Output Structure

KLEE generates several output files:

```
klee_output/
├── arithmetic/
│   ├── test000001.ktest    # Generated test case
│   ├── test000001.err      # Error details (if any)
│   ├── run.stats           # Execution statistics
│   └── klee.log           # KLEE execution log
├── strings/
│   └── ...
└── memory_pool/
    └── ...
```

### Analysis Report

The automated analysis generates `klee_analysis_report.md` with:

- Executive summary
- Test results for each harness
- Error details with reproduction steps
- Coverage statistics
- Recommendations for fixes

### Key Metrics

- **Test cases generated**: Number of unique execution paths explored
- **Errors found**: Critical issues requiring attention
- **Coverage**: Instruction and branch coverage achieved
- **Completed paths**: Paths that finished execution

## Interpreting Common Issues

### 1. Division by Zero

```
Error: KLEE: ERROR: test_arithmetic.c:45: division by zero
```

**Fix**: Add input validation before division operations:
```c
if (divisor != 0) {
    result = dividend / divisor;
} else {
    return ERROR_DIVISION_BY_ZERO;
}
```

### 2. Buffer Overflow

```
Error: KLEE: ERROR: test_strings.c:78: memory error: out of bound pointer
```

**Fix**: Add bounds checking:
```c
if (index < buffer_size) {
    buffer[index] = value;
} else {
    return ERROR_BUFFER_OVERFLOW;
}
```

### 3. Null Pointer Dereference

```
Error: KLEE: ERROR: test_validation.c:123: memory error: null pointer dereference
```

**Fix**: Add null checks:
```c
if (ptr != NULL) {
    return ptr->field;
} else {
    return ERROR_NULL_POINTER;
}
```

### 4. Integer Overflow

```
Error: KLEE: ERROR: test_arithmetic.c:89: overflow on signed integer operation
```

**Fix**: Use safe arithmetic or check bounds:
```c
if (a > 0 && b > INT_MAX - a) {
    return ERROR_OVERFLOW;
}
return a + b;
```

## Integration with CI/CD

### GitHub Actions

Add to `.github/workflows/klee.yml`:

```yaml
name: KLEE Analysis

on: [push, pull_request]

jobs:
  klee-analysis:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Install KLEE
      run: |
        sudo apt-get update
        sudo apt-get install klee
    
    - name: Run KLEE Analysis
      run: make klee-check
    
    - name: Upload Results
      uses: actions/upload-artifact@v3
      with:
        name: klee-results
        path: klee_output/
```

### Continuous Monitoring

Set up automated analysis:

```bash
# Add to crontab for nightly analysis
0 2 * * * cd /path/to/jubily && make klee-all && mail -s "KLEE Results" admin@example.com < klee_analysis_report.md
```

## Advanced Configuration

### KLEE Options

Key KLEE command-line options:

```bash
klee \
  --max-time=300              # 5-minute timeout
  --max-memory=1000           # 1GB memory limit
  --max-instructions=10000000 # Instruction limit
  --write-test-cases          # Generate test cases
  --write-paths               # Write path information
  --optimize                  # Enable optimizations
  --use-forked-solver         # Use forked solver
  --search=dfs                # Search strategy
  --libc=uclibc              # Standard library
  --posix-runtime            # POSIX environment
  input.bc
```

### Custom Test Harnesses

Create additional test harnesses:

```c
#include <klee/klee.h>

void test_custom_function() {
    int input;
    klee_make_symbolic(&input, sizeof(input), "custom_input");
    klee_assume(input >= 0 && input <= 1000);
    
    int result = custom_function(input);
    assert(result >= 0);  // Add your assertions
}

int main() {
    test_custom_function();
    return 0;
}
```

### Compilation Flags

Optimal compilation for KLEE:

```bash
klee-clang \
  -I. -Ilambda -Ilib \
  -emit-llvm \
  -c -g -O0 \
  -Xclang -disable-O0-optnone \
  -DKLEE_ANALYSIS \
  test.c -o test.bc
```

## Troubleshooting

### Common Issues

1. **KLEE not found**
   ```bash
   export PATH=/usr/local/bin:$PATH
   which klee
   ```

2. **Compilation errors**
   ```bash
   # Check include paths
   klee-clang -v test.c
   ```

3. **Runtime errors**
   ```bash
   # Enable verbose output
   klee --debug-print-instructions=all:stderr test.bc
   ```

4. **Out of memory**
   ```bash
   # Reduce memory usage
   klee --max-memory=500 test.bc
   ```

### Debug Mode

Enable KLEE debugging:

```bash
export KLEE_DEBUG=1
klee --debug-print-instructions=all:stderr test.bc
```

## Best Practices

1. **Start Small**: Begin with simple test harnesses
2. **Constrain Inputs**: Use `klee_assume()` to limit input space
3. **Add Assertions**: Use `assert()` for correctness properties
4. **Monitor Resources**: Set appropriate timeouts and memory limits
5. **Iterate**: Fix issues and re-run analysis
6. **Document**: Keep track of known issues and fixes

## Performance Optimization

### Input Space Reduction

```c
// Instead of unconstrained input
int x;
klee_make_symbolic(&x, sizeof(x), "x");

// Use constrained input
int x;
klee_make_symbolic(&x, sizeof(x), "x");
klee_assume(x >= 0 && x <= 100);
```

### Search Strategy

```bash
# Different search strategies
klee --search=dfs          # Depth-first (default)
klee --search=bfs          # Breadth-first
klee --search=random-state # Random
klee --search=nurs:covnew  # Coverage-optimized
```

### Parallel Analysis

```bash
# Run multiple tests in parallel
make -j4 klee-run
```

## Integration with Lambda Script

### Instrumented Functions

The test harnesses cover key Lambda Script components:

1. **Arithmetic Operations** (`lambda/lambda-eval.cpp`)
   - Division and modulo operations
   - Arithmetic overflow detection
   - Type conversion safety

2. **String Processing** (`lambda/unicode_string.cpp`)
   - Buffer management
   - Unicode handling
   - String manipulation safety

3. **Array Operations** (`lambda/lambda-data.hpp`)
   - Bounds checking
   - Memory allocation
   - Index validation

4. **Memory Management** (`lib/mem-pool/`)
   - Pool allocation
   - Reference counting
   - Memory leak detection

5. **Data Validation** (`lambda/validator/`)
   - Type checking
   - Schema validation
   - Input sanitization

### Test Coverage Goals

- **90%+ instruction coverage** for core components
- **All error paths tested** with symbolic inputs
- **Boundary conditions verified** for all data structures
- **Null pointer scenarios covered** for all APIs

## Conclusion

KLEE integration provides automated discovery of critical runtime issues in Lambda Script. Regular analysis helps maintain code quality and prevents security vulnerabilities. The comprehensive test harnesses and automation scripts make it easy to integrate KLEE into the development workflow.

For questions or issues, refer to:
- [KLEE Documentation](https://klee.github.io/)
- [LLVM Documentation](https://llvm.org/docs/)
- Lambda Script project documentation
