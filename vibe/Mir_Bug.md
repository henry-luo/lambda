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

**Fixed (workaround applied)** — 2025-02-09

The root cause is a **lost-copy bug in MIR's SSA destruction** at optimization levels ≥ 2. The transpiler now works around it by emitting `*(&_x)=value` instead of `_x=value` for native scalar assignments inside while loops, forcing MIR to use memory store/load operations.

### Root Cause Analysis

**The bug is in MIR's GVN + Copy Propagation + SSA destruction pipeline:**

1. **SSA Construction**: MIR correctly builds SSA form with phi nodes:
   ```
   phi  b@1 = phi(b_init, b@2)
   phi  a@1 = phi(a_init, a@2)
   i_1 = a@1 + b@1        // temp = a + b
   a@2 = b@1              // a = old b
   b@2 = i_1              // b = temp
   ```

2. **GVN removes copies**: `temp@1 → i_1`, `b@2 → i_1`, `i@2 → i_2`

3. **Copy Propagation**: `a@2 = b@1` → propagated into phi: `phi a@1 = phi(a_init, b@1)`

4. **SSA Destruction (the bug)**: When lowering out of SSA, MIR emits phi-resolution copies:
   ```
   b = a + b           // b now = temp = a+b  (insn 27: b@1%0 = i_1)
   a = b               // a = NEW b = a+b     (insn 29: a@1%0 = b@1%0) ← WRONG!
   ```
   Instruction 29 reads `b@1%0` **after** instruction 27 overwrote it. The SSA destruction
   should have emitted these copies in the correct order or used a temporary to break the cycle.

5. **Register Allocation compounds it**: After RA, `t2` (constant 1) shares a register with `a`,
   further clobbering values.

This is a classic **lost-copy problem** in SSA destruction — the phi-resolution copies form a cycle
(`a → b → a`) and MIR fails to detect and break this cycle with a temporary.

**Optimization levels affected**: 2 (adds CSE/GVN + CCP) and 3 (adds LICM + register renaming).

### Applied Fix

In `lambda/transpile.cpp`, the transpiler now uses `*(&_x)=value` for native scalar (int, int64,
float, bool) variable assignments inside while loops. This forces MIR to emit memory store/load
instructions which prevent the SSA optimizer from creating the problematic phi-copy cycle.

**Generated C code before fix:**
```c
while (_i < 3) {
    _temp = _a + _b;     // MIR optimizer mishandles this pattern
    _a = _b;
    _b = _temp;
    _i = _i + 1;
}
```

**Generated C code after fix:**
```c
while (_i < 3) {
    *(&_temp) = _a + _b;   // memory write prevents SSA optimization bug
    *(&_a) = _b;
    *(&_b) = _temp;
    *(&_i) = _i + 1;
}
```

The `*(&x)` pattern is semantically identical to `x` but prevents MIR's optimizer from treating
the variable as a pure SSA register, forcing correct memory-order semantics.

**Files changed:**
- `lambda/ast.hpp` — Added `while_depth` field to `Transpiler` struct
- `lambda/transpile.cpp` — `transpile_while()` tracks depth; `transpile_assign_stam()` uses
  `*(&_x)=` pattern for native types when `while_depth > 0`

### Workaround Evaluation Summary

| Approach | Works? | Overhead | Notes |
|----------|--------|----------|-------|
| `*(&x) = value` (applied) | ✅ | Negligible | Forces memory store, transparent to semantics |
| Pointer variables (`int32_t *pa = &a`) | ✅ | Low | Requires extra declarations |
| Algebraic swap (`b=a+b; a=b-a`) | ✅ | None | Only works for addition-based swaps |
| Array-based (`vars[0], vars[1]`) | ✅ | Low | Forces memory access |
| `volatile` keyword | ❌ | N/A | MIR ignores volatile |
| Extra temp variable (`old_b`) | ❌ | N/A | Same SSA pattern, same bug |
| Address escape (unused `&x`) | ❌ | N/A | Optimizer still promotes to register |

### Notes

- The transpiler changes to add newlines before `while` and `return` statements were made during investigation but did not fix the issue (the formatting was not the cause)
- The `-O0` flag causes a separate issue where `run_main` doesn't execute properly
- Test files: `temp/test_while_3.ls`, `temp/test_while_comprehensive.ls`, `temp/test_mir_debug.c`, `temp/test_mir_workarounds.c`
