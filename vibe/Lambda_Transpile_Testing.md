# GTest for Transpiled C Code Verification

## Overview

This document describes the testing strategy for verifying that Lambda's transpiler generates correct C code patterns, specifically for unboxed function optimizations.

**Status: ✅ Implemented**

## Problem Statement

Lambda tests verify **execution results** (output matching), but not the **generated code quality**. We need to verify:
1. Unboxed functions (e.g., `fn_pow_u`, `fn_abs_i`) are called when expected
2. Boxing/unboxing overhead is eliminated for typed parameters
3. Native C operators are used instead of runtime function calls when type information is available

## Implemented Solution: Fixture File with Expected Patterns

### Location
- **Test runner**: `test/test_transpile_patterns_gtest.cpp`
- **Fixtures**: `test/lambda/*.transpile` (alongside `.ls` test scripts)

### Fixture Format
For a script `abc.ls`, create `abc.transpile` as a JSON file:

```json
{
    "expect": [
        "fn_pow_u",
        "push_d(fn_pow_u",
        "fn_abs_i"
    ],
    "forbid": [
        "= fn_pow(",
        "return fn_pow("
    ]
}
```

- **`expect`**: Patterns that MUST appear in the transpiled C code
- **`forbid`**: Patterns that must NOT appear in the transpiled C code

### How It Works

1. Test discovery scans `test/lambda/` for `*.transpile` files
2. For each fixture, the corresponding `.ls` script is transpiled via `lambda.exe`
3. The generated `_transpiled_0.c` is read
4. Each pattern in `expect` is checked for presence
5. Each pattern in `forbid` is checked for absence

### Example Fixtures

#### `test/lambda/unboxed_sys_func.transpile`
```json
{
    "expect": [
        "fn_pow_u",
        "fn_min2_u",
        "fn_max2_u",
        "fn_abs_i",
        "fn_sign_i",
        "fn_sign_f"
    ],
    "forbid": [
        "= fn_pow(",
        "return fn_pow("
    ]
}
```

#### `test/lambda/sys_func_native_math.transpile`
```json
{
    "expect": [
        "sin(",
        "cos(",
        "sqrt(",
        "fabs(",
        "fn_abs_i"
    ],
    "forbid": []
}
```

### Adding New Tests

1. Create or identify an existing `.ls` test script
2. Create a matching `.transpile` JSON file with expect/forbid patterns
3. Run `make test-lambda-baseline` to verify

### Pattern Tips

**Avoid matching declarations**: Function declarations appear as `Item fn_pow(Item a, Item b);`. To match only calls, use patterns like:
- `= fn_pow(` (assignment from call)
- `return fn_pow(` (return statement)
- `push_d(fn_pow_u` (specific call pattern)

**Flexible matching**: Patterns are simple substring matches. For regex support, the test framework could be extended.

## Running Tests

```bash
# Run transpile pattern tests only
./test/test_transpile_patterns_gtest.exe

# Run all baseline tests (includes transpile patterns)
make test-lambda-baseline
```

## Files

| File | Purpose |
|------|---------|
| `test/test_transpile_patterns_gtest.cpp` | Test runner with auto-discovery |
| `test/lambda/*.transpile` | JSON fixture files |
| `test/lambda/unboxed_sys_func.ls` | Test script for unboxed functions |
| `test/lambda/unboxed_sys_func.txt` | Expected output |
| `test/lambda/unboxed_sys_func.transpile` | Transpile pattern fixture |
