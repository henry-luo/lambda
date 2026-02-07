# MIR JIT Known Issues

## Issue: Incorrect Results with Three-Way Swap Pattern in Loops

**Discovered:** 2025-02-07

**Severity:** High - produces incorrect computation results silently

### Description

MIR JIT's optimizer produces incorrect results when a while loop contains a classic three-way variable swap pattern that uses all variables being modified in the computation.

### Minimal Reproduction

**Lambda code:**
```lambda
pn test() {
    var a: int = 0
    var b: int = 1
    var temp: int = 0
    var i: int = 0
    
    while (i < 3) {
        temp = a + b
        a = b
        b = temp
        i = i + 1
    }
    
    return b   // Expected: 3, Actual: 4
}
```

**Transpiled C code (correct):**
```c
int32_t test() {
    int32_t a = 0;
    int32_t b = 1;
    int32_t temp = 0;
    int32_t i = 0;
    
    while (i < 3) {
        temp = a + b;
        a = b;
        b = temp;
        i = i + 1;
    }
    
    return b;
}
```

### Evidence

| Compiler | Result | Expected | Status |
|----------|--------|----------|--------|
| Standard `cc` (clang/gcc) | 3 | 3 | ✅ Correct |
| MIR JIT (optimize level 2) | 4 | 3 | ❌ Wrong |
| MIR JIT with print statements | 3 | 3 | ✅ Correct |

### Characteristics

1. **Threshold:** Bug appears after 2+ iterations (2 iterations work correctly)
2. **Pattern:** The problematic pattern is:
   ```c
   temp = a + b;  // uses both a AND b
   a = b;
   b = temp;
   ```
3. **Workaround Effect:** Adding `print()` calls inside the loop forces variable spilling to memory, which makes the computation correct
4. **Optimization Level:** Occurs at default optimization level 2

### Root Cause Hypothesis

MIR's optimizer likely performs incorrect register allocation or code motion optimization for this specific pattern. The optimizer may:
- Incorrectly reuse a register that still holds a stale value
- Perform loop-invariant code motion incorrectly
- Have a bug in its SSA (Static Single Assignment) transformation for this pattern

The fact that adding print statements (which force memory spills) fixes the issue strongly suggests a register allocation bug.

### Workarounds

**Workaround 1: Algebraic Swap (Recommended)**

Avoid the temp variable by using algebraic transformation:

```lambda
pn fib(n: int) {
    if (n <= 1) { return n }
    var a: int = 0
    var b: int = 1
    var i: int = 2
    while (i <= n) {
        b = a + b    // b = new_b
        a = b - a    // a = old_b (new_b - old_a = old_b)
        i = i + 1
    }
    return b
}
```

**Workaround 2: Add Dummy Operation**

Force variable spilling by adding a side-effect operation:

```lambda
while (i < n) {
    temp = a + b
    a = b
    b = temp
    i = i + 1
    if (false) { print("") }  // Never executes but prevents optimization
}
```

**Workaround 3: Use Print Tracing**

If debugging, keep print statements which will make the computation correct:

```lambda
while (i < n) {
    print("i=", i, " ")  // Forces correct behavior
    temp = a + b
    a = b
    b = temp
    i = i + 1
}
```

### Affected Algorithms

Any algorithm using the classic swap-with-temp pattern in a loop:
- Fibonacci sequence (iterative)
- Bubble sort
- Selection sort
- Any variable rotation pattern

### MIR Version

```
Copyright (C) 2018-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>
```

### Related Files

- `lambda/mir.c` - MIR integration
- `lambda/transpile.cpp` - C code generation
- `include/mir.h` - MIR API headers

### Testing

Test file: `temp/test_while_3.ls`

```bash
# Demonstrates the bug
./lambda.exe run temp/test_while_3.ls
# Output: b=4 (wrong, should be 3)

# Verify with standard C compiler
cd temp && cc -o test_while_3 test_while_3.c && ./test_while_3
# Output: b = 3 (correct)
```

### Status

**Not Fixed** - This is a bug in the external MIR library, not in Lambda's transpiler. The generated C code is correct; MIR's JIT compilation produces wrong machine code.

### Potential Solutions

1. **Report to MIR maintainer** - File an issue with the MIR project
2. **Downgrade optimization** - Use `--optimize=0` (but this breaks `run_main`)
3. **Pattern detection** - Detect this pattern in transpiler and apply workaround automatically
4. **Alternative JIT** - Consider LLVM or other JIT backends for complex code

### Notes

- The transpiler changes to add newlines before `while` and `return` statements were made during investigation but did not fix the issue (the formatting was not the cause)
- The `-O0` flag causes a separate issue where `run_main` doesn't execute properly
