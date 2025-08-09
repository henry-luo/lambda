# Isolated Test for Null Byte Error

This directory contains the minimal test case that reproduces the null byte/crash error in the LaTeX parser.

## Test Files

- `isolated_null_test.ls` - Lambda script that runs the test
- `minimal_crash_test.tex` - Minimal LaTeX content that triggers the bug

## How to Run

From the project root directory:

```bash
./lambda.exe test/isolated/isolated_null_test.ls
```

## What This Test Reveals

This minimal test case demonstrates:

1. **Memory corruption in LaTeX parser** when processing multiple math environments
2. **Buffer management issues** with the StrBuf patterns in `input-latex.cpp`
3. **Crash in JSON formatter** when trying to access corrupted string data

## Root Cause

The error-prone patterns in `lambda/input/input-latex.cpp`:

```cpp
// PROBLEMATIC PATTERN:
String *var = (String*)sb->str;
var->len = sb->length - sizeof(uint32_t);
var->ref_cnt = 0;
strbuf_full_reset(sb);
```

These patterns corrupt memory when used repeatedly across multiple math environments, leading to crashes or massive null byte output.

## Expected Fix

Replace the problematic patterns with:

```cpp
// CORRECT PATTERN:
String *var = strbuf_to_string(sb);
if (var && var->len > 0) {
    // use var
}
```
