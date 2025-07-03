# MIR JIT Test Suite

This test suite provides comprehensive testing for the MIR (Medium Intermediate Representation) JIT compilation functionality in the Jubily project.

## Overview

The MIR JIT system allows for dynamic compilation of C code at runtime using the MIR library. This test suite validates the core functionality including:

- JIT context initialization and cleanup
- C code compilation to MIR intermediate representation
- Native code generation from MIR
- Function execution and result verification
- Error handling for edge cases

## Files

- `test_mir.c` - Main test file containing Criterion-based test cases
- `test_mir.sh` - Shell script to build and run the tests
- `test_mir_README.md` - This documentation file

## Test Cases

### 1. Basic Functionality Tests

- **test_jit_init_cleanup**: Verifies MIR context initialization and cleanup
- **test_jit_compile_hello_world**: Tests compilation of a simple function returning a constant
- **test_jit_compile_math_function**: Tests compilation of a basic math function
- **test_jit_compile_return_value**: Tests function execution and return value verification

### 2. Advanced Tests

- **test_jit_compile_multiple_functions**: Tests compilation of multiple functions in separate contexts
- **test_jit_with_variables**: Tests compilation and execution of functions with local variables

### 3. Error Handling Tests

- **test_jit_nonexistent_function**: Tests error handling for non-existent function lookup
- **test_jit_empty_code**: Tests compilation behavior with empty code

## Prerequisites

Before running the tests, ensure you have:

1. **MIR library** installed at `/usr/local/lib/libmir.a` with headers in `/usr/local/include`
2. **Criterion testing framework** installed (available via Homebrew: `brew install criterion`)
3. **zlog library** for logging functionality
4. **Project built** - Run `./compile-lambda.sh` from the project root to build required object files

## Running the Tests

### Quick Run

```bash
cd test
./test_mir.sh
```

### Manual Compilation

If you need to compile manually:

```bash
cd /path/to/jubily/project
gcc -std=c99 -Wall -Wextra -O2 -g -fms-extensions -pedantic \
    -I/usr/local/include -Ilambda/tree-sitter/lib/include \
    -o test/test_mir.exe test/test_mir.c \
    build/*.o \
    lambda/tree-sitter/libtree-sitter.a \
    /usr/local/lib/libmir.a \
    /usr/local/lib/libzlog.a \
    -L/opt/homebrew/lib -lgmp -lcriterion
```

## Test Output

The test suite provides detailed output including:
- Compilation progress for each test case
- MIR compilation timing information
- Function generation success/failure
- Test results summary

Example successful output:
```
[====] Synthesis: Tested: 8 | Passing: 8 | Failing: 0 | Crashing: 0 
All MIR tests passed!
```

## Test Coverage

The test suite covers:

✅ **JIT Context Management**: Initialization, cleanup, and resource management  
✅ **C Code Compilation**: Converting C source code to MIR intermediate representation  
✅ **Native Code Generation**: Generating executable machine code from MIR  
✅ **Function Execution**: Calling JIT-compiled functions and verifying results  
✅ **Error Handling**: Proper handling of compilation errors and missing functions  
✅ **Memory Management**: Ensuring no memory leaks in JIT operations  
✅ **Multiple Functions**: Handling compilation of multiple functions  
✅ **Variable Usage**: Testing functions with local variables and computations  

## Integration with CI/CD

The test script returns appropriate exit codes:
- `0` - All tests passed
- `1` - Some tests failed
- `>1` - Compilation or system errors

This makes it suitable for integration with continuous integration systems.

## Troubleshooting

### Common Issues

1. **Missing MIR Library**: 
   - Install MIR library: `brew install mir` or build from source
   - Verify installation: `ls /usr/local/lib/libmir.a`

2. **Missing Criterion**: 
   - Install Criterion: `brew install criterion`
   - Verify installation: `pkg-config --exists criterion`

3. **Compilation Errors**:
   - Ensure project is built: `./compile-lambda.sh`
   - Check object files: `ls build/`

4. **Function Not Found Errors**:
   - Verify function names match exactly
   - Check that functions are properly exported in the C code

### Debug Mode

For debugging, you can run individual test cases or add verbose output by modifying the test files or using GDB:

```bash
gdb ./test_mir.exe
```

## Future Enhancements

Potential improvements to the test suite:

- [ ] Performance benchmarking tests
- [ ] Memory usage validation
- [ ] Complex data structure handling
- [ ] Multi-threading safety tests
- [ ] Integration with lambda language features
- [ ] Cross-platform compatibility tests

## Contributing

When adding new tests:

1. Follow the existing naming convention: `test_jit_<functionality>`
2. Include both positive and negative test cases
3. Clean up resources properly in each test
4. Update this README with new test descriptions
5. Ensure tests are deterministic and repeatable

## License

This test suite is part of the Jubily project and follows the same licensing terms.
